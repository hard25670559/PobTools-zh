// POSIX implementation of the headless transport declared in headless_ipc.h.
//
// Platform counterpart to headless_ipc.cpp, selected by the build on anything
// that is not Windows (see sys_console.cpp / sys_console_unix.cpp for the same
// pattern). The wire protocol, the queueing and the JSON helpers behave exactly
// as the Windows version does; only the three things that have no portable form
// differ:
//
//   - std handles      -> the standard descriptors, 0 and 1
//   - "is it a console" -> isatty(), where Windows asks GetFileType
//   - cancelling a read -> poll() with a timeout, so the reader is never parked
//                          in a blocking read and Stop() needs no equivalent of
//                          CancelSynchronousIo (nor its detach-and-hope path)
//
// One hazard the Windows version does not have: writing to a pipe whose reader
// has gone raises SIGPIPE, which kills the process by default. Start() ignores
// it so a departed host surfaces as HostGone() -- the same outcome the Windows
// version reaches through a failed WriteFile.

#include "headless_ipc.h"

#include <atomic>
#include <cerrno>
#include <csignal>
#include <cstdio>
#include <deque>
#include <fcntl.h>
#include <mutex>
#include <poll.h>
#include <thread>
#include <unistd.h>

namespace HeadlessIpc {

namespace {

// How long the reader parks in poll() before re-checking whether Stop() was
// called. Matches the 50 ms the Windows version waits per cancel attempt.
constexpr int kPollTimeoutMs = 50;

std::mutex g_inMutex;
std::deque<std::string> g_inQueue;
std::atomic<bool> g_hostGone{false};
std::atomic<bool> g_active{false};
std::thread* g_reader = nullptr; // heap pointer: see hang_watch.cpp for why not a static object
std::mutex g_outMutex;

void ReaderProc()
{
	std::string partial;
	char buf[8192];
	while (g_active) {
		pollfd pfd{};
		pfd.fd = STDIN_FILENO;
		pfd.events = POLLIN;

		const int ready = ::poll(&pfd, 1, kPollTimeoutMs);
		if (ready < 0) {
			if (errno == EINTR) continue;
			break; // the descriptor is unusable; treat it as the host going away
		}
		if (ready == 0) continue; // timed out: loop back and re-check g_active

		// POLLHUP without POLLIN means the writer closed with nothing left to
		// read. With POLLIN still set there is buffered data to drain first.
		if ((pfd.revents & POLLIN) == 0) break;

		const ssize_t got = ::read(STDIN_FILENO, buf, sizeof(buf));
		if (got < 0) {
			if (errno == EINTR || errno == EAGAIN) continue;
			break;
		}
		if (got == 0) break; // EOF: the host closed our stdin

		partial.append(buf, static_cast<size_t>(got));
		size_t start = 0;
		for (;;) {
			size_t nl = partial.find('\n', start);
			if (nl == std::string::npos) break;
			std::string line = partial.substr(start, nl - start);
			if (!line.empty() && line.back() == '\r') line.pop_back();
			start = nl + 1;
			if (line.empty()) continue;
			std::lock_guard<std::mutex> lock(g_inMutex);
			g_inQueue.push_back(std::move(line));
		}
		partial.erase(0, start);
	}
	g_hostGone = true;
}

} // namespace

bool Start()
{
	if (g_active) return true;

	// A pipe or a file, never a terminal: a terminal stdin would wait for a
	// keyboard, and there would be nobody on the other end to answer.
	if (::isatty(STDIN_FILENO) || ::isatty(STDOUT_FILENO)) {
		return false;
	}
	// Both descriptors must actually be open.
	if (::fcntl(STDIN_FILENO, F_GETFD) < 0 || ::fcntl(STDOUT_FILENO, F_GETFD) < 0) {
		return false;
	}

	// See the header comment: without this, a host that exits first takes the
	// engine down with a signal instead of letting Send() report HostGone().
	::signal(SIGPIPE, SIG_IGN);

	g_hostGone = false;
	g_active = true;
	g_reader = new std::thread(ReaderProc);
	return true;
}

void Stop()
{
	if (!g_active) return;
	g_active = false;
	// The reader is in poll() with a timeout, not a blocking read, so clearing
	// the flag is enough: it returns within kPollTimeoutMs and exits on its own.
	// Nothing to cancel, and the join always completes.
	if (g_reader) {
		if (g_reader->joinable()) g_reader->join();
		delete g_reader;
		g_reader = nullptr;
	}
	// Deliberately not closing descriptor 0. The Windows version closes its
	// stdin handle, but on POSIX a freed descriptor 0 is handed to the next
	// open() in this process, which would silently alias an unrelated file to
	// standard input. The reader has already stopped, so there is nothing left
	// to guard against.
}

bool Active() { return g_active; }

bool Pop(std::string& line)
{
	std::lock_guard<std::mutex> lock(g_inMutex);
	if (g_inQueue.empty()) return false;
	line = std::move(g_inQueue.front());
	g_inQueue.pop_front();
	return true;
}

bool HostGone() { return g_hostGone; }

void Send(const std::string& json)
{
	if (!g_active) return;
	std::lock_guard<std::mutex> lock(g_outMutex);
	std::string out = json;
	out.push_back('\n');
	const char* p = out.data();
	size_t left = out.size();
	while (left > 0) {
		const ssize_t wrote = ::write(STDOUT_FILENO, p, left);
		if (wrote < 0) {
			if (errno == EINTR) continue;
			g_hostGone = true;
			return;
		}
		if (wrote == 0) {
			g_hostGone = true;
			return;
		}
		p += wrote;
		left -= static_cast<size_t>(wrote);
	}
	// No flush: write() on a pipe or a file hands the bytes to the kernel
	// immediately. fsync() would be the literal counterpart of
	// FlushFileBuffers, but it fails with EINVAL on a pipe, which is the
	// transport this is built for.
}

std::string Quote(const std::string& s)
{
	std::string out;
	out.reserve(s.size() + 2);
	out.push_back('"');
	for (unsigned char c : s) {
		switch (c) {
		case '"': out += "\\\""; break;
		case '\\': out += "\\\\"; break;
		case '\n': out += "\\n"; break;
		case '\r': out += "\\r"; break;
		case '\t': out += "\\t"; break;
		default:
			if (c < 0x20) {
				char hex[8];
				snprintf(hex, sizeof(hex), "\\u%04x", c);
				out += hex;
			} else {
				out.push_back((char)c);
			}
		}
	}
	out.push_back('"');
	return out;
}

void SendEvent(const char* event, const std::string& dataJson)
{
	Send("{\"event\":" + Quote(event) + ",\"data\":" + (dataJson.empty() ? "null" : dataJson) + "}");
}

void SendResult(long long id, const std::string& resultJson)
{
	Send("{\"id\":" + std::to_string(id) + ",\"result\":" + (resultJson.empty() ? "null" : resultJson) + "}");
}

void SendError(long long id, const char* code, const std::string& message)
{
	Send("{\"id\":" + std::to_string(id) + ",\"error\":{\"code\":" + Quote(code) +
	     ",\"message\":" + Quote(message) + "}}");
}

} // namespace HeadlessIpc
