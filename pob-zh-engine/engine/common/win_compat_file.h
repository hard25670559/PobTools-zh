// SimpleGraphic Engine
//
// Win32 file and time API shims for non-Windows targets.
//
// Split from win_compat.h because this half is bulkier and only a few files
// need it: the ones that write diagnostic logs through the wide-character
// Win32 file calls (host/error_log.cpp, translate/startup_trace.cpp).
//
// Keeping their logic untouched is the point. The log rotation, the dedupe
// rules and the file naming are all behaviour worth preserving exactly, so the
// calls they make are reproduced here instead of the files being rewritten.
//
// HANDLE carries a file descriptor, stored as fd+1 so that descriptor 0 is not
// mistaken for a null handle. Only the operations the engine actually performs
// are implemented; anything else is deliberately absent rather than wrong.

#pragma once

#if !defined(_WIN32)

// Included from the end of win_compat.h, which is the single entry point for
// the whole shim: the integer typedefs and GetModuleFileNameA it provides are
// already in scope by the time this file is read.

#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <dlfcn.h>
#include <ctime>
#include <dirent.h>
#include <fcntl.h>
#include <fnmatch.h>
#include <filesystem>
#include <string>
#include <vector>
#include <sys/resource.h>
#include <sys/file.h>
#include <sys/statvfs.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <signal.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#if defined(__APPLE__)
#include <sys/sysctl.h>
#endif

// ---------------------------------------------------------------- handles

using HANDLE = void*;

#if !defined(INVALID_HANDLE_VALUE)
#define INVALID_HANDLE_VALUE (reinterpret_cast<HANDLE>(static_cast<intptr_t>(-1)))
#endif

namespace pob_win_compat {

inline HANDLE FdToHandle(int fd)
{
	return reinterpret_cast<HANDLE>(static_cast<intptr_t>(fd) + 1);
}

inline int HandleToFd(HANDLE h)
{
	if (h == INVALID_HANDLE_VALUE || h == nullptr) return -1;
	return static_cast<int>(reinterpret_cast<intptr_t>(h)) - 1;
}

// The engine's wide strings are paths and format output, which are UTF-16 on
// Windows and UTF-32 here. Converting by hand rather than through wcstombs
// keeps the result correct regardless of the process locale, which nothing
// sets.
inline std::string WideToUtf8(const wchar_t* w)
{
	std::string out;
	if (w == nullptr) return out;
	for (const wchar_t* p = w; *p != L'\0'; ++p) {
		const uint32_t c = static_cast<uint32_t>(*p);
		if (c < 0x80) {
			out.push_back(static_cast<char>(c));
		} else if (c < 0x800) {
			out.push_back(static_cast<char>(0xC0 | (c >> 6)));
			out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
		} else if (c < 0x10000) {
			out.push_back(static_cast<char>(0xE0 | (c >> 12)));
			out.push_back(static_cast<char>(0x80 | ((c >> 6) & 0x3F)));
			out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
		} else {
			out.push_back(static_cast<char>(0xF0 | (c >> 18)));
			out.push_back(static_cast<char>(0x80 | ((c >> 12) & 0x3F)));
			out.push_back(static_cast<char>(0x80 | ((c >> 6) & 0x3F)));
			out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
		}
	}
	return out;
}


// The path form of WideToUtf8: use this wherever the string is a path.
inline std::string WideToPath(const wchar_t* w)
{
	return NormalizePath(WideToUtf8(w));
}

inline size_t Utf8ToWide(const char* s, wchar_t* buffer, size_t size)
{
	if (buffer == nullptr || size == 0) return 0;
	size_t out = 0;
	const unsigned char* p = reinterpret_cast<const unsigned char*>(s);
	while (*p != 0 && out + 1 < size) {
		uint32_t c = *p;
		int extra = 0;
		if (c >= 0xF0)      { c &= 0x07; extra = 3; }
		else if (c >= 0xE0) { c &= 0x0F; extra = 2; }
		else if (c >= 0xC0) { c &= 0x1F; extra = 1; }
		++p;
		for (int i = 0; i < extra && *p != 0; ++i, ++p) {
			c = (c << 6) | (*p & 0x3F);
		}
		buffer[out++] = static_cast<wchar_t>(c);
	}
	buffer[out] = L'\0';
	return out;
}

} // namespace pob_win_compat

#if !defined(FILE_ATTRIBUTE_REPARSE_POINT)
#define FILE_ATTRIBUTE_REPARSE_POINT 0x00000400u
#endif
#if !defined(IO_REPARSE_TAG_SYMLINK)
#define IO_REPARSE_TAG_SYMLINK 0xA000000Cu
#endif
#if !defined(IO_REPARSE_TAG_MOUNT_POINT)
#define IO_REPARSE_TAG_MOUNT_POINT 0xA0000003u
#endif

// ------------------------------------------------- CreateFile flag values

// The real Win32 values, because call sites combine them with bitwise or.
#if !defined(GENERIC_READ)
#define GENERIC_READ 0x80000000u
#endif
#if !defined(GENERIC_WRITE)
#define GENERIC_WRITE 0x40000000u
#endif
#if !defined(FILE_APPEND_DATA)
#define FILE_APPEND_DATA 0x00000004u
#endif
#if !defined(FILE_SHARE_READ)
#define FILE_SHARE_READ 0x00000001u
#endif
#if !defined(FILE_SHARE_WRITE)
#define FILE_SHARE_WRITE 0x00000002u
#endif
#if !defined(CREATE_ALWAYS)
#define CREATE_ALWAYS 2u
#endif
#if !defined(OPEN_ALWAYS)
#define OPEN_ALWAYS 4u
#endif
#if !defined(OPEN_EXISTING)
#define OPEN_EXISTING 3u
#endif
#if !defined(ERROR_SHARING_VIOLATION)
#define ERROR_SHARING_VIOLATION 32u
#endif

// --------------------------------------------------------------- events

#if !defined(WAIT_OBJECT_0)
#define WAIT_OBJECT_0 0u
#endif
#if !defined(WAIT_TIMEOUT)
#define WAIT_TIMEOUT 258u
#endif
#if !defined(WAIT_FAILED)
#define WAIT_FAILED 0xFFFFFFFFu
#endif

namespace pob_win_compat {

// An event handle points to one of these; a file handle is a small integer
// (fd+1). The magic field tells them apart for the calls that accept either,
// and the range check below keeps a file handle from ever being dereferenced.
struct EventState {
	static constexpr unsigned int kMagic = 0x45565431u; // "EVT1"
	unsigned int magic = kMagic;
	std::mutex mx;
	std::condition_variable cv;
	bool signaled = false;
	bool manualReset = true;
};

inline EventState* AsEvent(HANDLE h)
{
	const uintptr_t v = reinterpret_cast<uintptr_t>(h);
	if (h == nullptr || h == INVALID_HANDLE_VALUE || v < 0x10000) return nullptr;
	auto* candidate = reinterpret_cast<EventState*>(h);
	return candidate->magic == EventState::kMagic ? candidate : nullptr;
}

} // namespace pob_win_compat

// Win32: an event the engine uses for one thing -- an interruptible sleep, so
// the watchdog wakes immediately on Stop() instead of finishing its wait.
inline HANDLE CreateEventW(void* security, int manualReset, int initialState, const wchar_t* name)
{
	(void)security; (void)name;
	auto* state = new pob_win_compat::EventState();
	state->manualReset = manualReset != 0;
	state->signaled = initialState != 0;
	return reinterpret_cast<HANDLE>(state);
}

inline int SetEvent(HANDLE h)
{
	auto* state = pob_win_compat::AsEvent(h);
	if (state == nullptr) return 0;
	{
		std::lock_guard<std::mutex> lock(state->mx);
		state->signaled = true;
	}
	state->cv.notify_all();
	return 1;
}

inline int ResetEvent(HANDLE h)
{
	auto* state = pob_win_compat::AsEvent(h);
	if (state == nullptr) return 0;
	std::lock_guard<std::mutex> lock(state->mx);
	state->signaled = false;
	return 1;
}

// Win32: waits for the handle to be signalled, or for the timeout. Only events
// are waitable here; a wait on anything else reports failure rather than
// pretending to have waited.
inline unsigned long WaitForSingleObject(HANDLE h, unsigned long milliseconds)
{
	auto* state = pob_win_compat::AsEvent(h);
	if (state == nullptr) return WAIT_FAILED;
	std::unique_lock<std::mutex> lock(state->mx);
	const auto ready = [state] { return state->signaled; };
	bool signalled = false;
	if (milliseconds == 0xFFFFFFFFu) {
		state->cv.wait(lock, ready);
		signalled = true;
	} else {
		signalled = state->cv.wait_for(lock, std::chrono::milliseconds(milliseconds), ready);
	}
	if (!signalled) return WAIT_TIMEOUT;
	if (!state->manualReset) state->signaled = false;
	return WAIT_OBJECT_0;
}

// --------------------------------------------------------- file operations

// Win32: opens or creates a file. Only the access and disposition combinations
// the engine uses are honoured. Share modes have no POSIX equivalent and are
// ignored: advisory locking is not what the Windows code is relying on here --
// it only wants concurrent readers, which POSIX allows unconditionally.
inline HANDLE CreateFileW(const wchar_t* path, unsigned long access, unsigned long shareMode,
                          void* security, unsigned long disposition,
                          unsigned long flags, HANDLE templateFile)
{
	(void)shareMode; (void)security; (void)flags; (void)templateFile;
	if (path == nullptr) return INVALID_HANDLE_VALUE;

	int oflag = 0;
	const bool wants_write = (access & (GENERIC_WRITE | FILE_APPEND_DATA)) != 0;
	const bool wants_read = (access & GENERIC_READ) != 0;
	if (wants_write && wants_read)      oflag = O_RDWR;
	else if (wants_write)               oflag = O_WRONLY;
	else                                oflag = O_RDONLY;

	if ((access & FILE_APPEND_DATA) != 0) oflag |= O_APPEND;
	if (disposition == CREATE_ALWAYS)     oflag |= O_CREAT | O_TRUNC;
	else if (disposition == OPEN_ALWAYS)  oflag |= O_CREAT;

	const std::string utf8 = pob_win_compat::WideToPath(path);
	const int fd = ::open(utf8.c_str(), oflag, 0644);
	if (fd < 0) return INVALID_HANDLE_VALUE;
	return pob_win_compat::FdToHandle(fd);
}

inline HANDLE CreateFileA(const char* path, unsigned long access, unsigned long shareMode,
                          void* security, unsigned long disposition,
                          unsigned long flags, HANDLE templateFile)
{
	if (path == nullptr) return INVALID_HANDLE_VALUE;
	wchar_t wide[4096];
	pob_win_compat::Utf8ToWide(path, wide, 4096);
	return CreateFileW(wide, access, shareMode, security, disposition, flags, templateFile);
}

inline int WriteFile(HANDLE h, const void* data, unsigned long size,
                     uint32_t* written, void* overlapped)
{
	(void)overlapped;
	if (written != nullptr) *written = 0;
	const int fd = pob_win_compat::HandleToFd(h);
	if (fd < 0 || data == nullptr) return 0;

	const char* p = static_cast<const char*>(data);
	unsigned long left = size;
	while (left > 0) {
		const ssize_t n = ::write(fd, p, left);
		if (n < 0) {
			if (errno == EINTR) continue;
			return 0;
		}
		if (n == 0) break;
		p += n;
		left -= static_cast<unsigned long>(n);
	}
	if (written != nullptr) *written = size - left;
	return 1;
}

inline int ReadFile(HANDLE h, void* buffer, unsigned long size,
                    uint32_t* read, void* overlapped)
{
	(void)overlapped;
	if (read != nullptr) *read = 0;
	const int fd = pob_win_compat::HandleToFd(h);
	if (fd < 0 || buffer == nullptr) return 0;
	for (;;) {
		const ssize_t n = ::read(fd, buffer, size);
		if (n < 0) {
			if (errno == EINTR) continue;
			return 0;
		}
		if (read != nullptr) *read = static_cast<unsigned long>(n);
		return 1;
	}
}

namespace pob_win_compat {
// Defined further down, beside the named-mutex machinery it needs.
inline bool CloseMutexHandle(HANDLE h);
}

inline int CloseHandle(HANDLE h)
{
	// Events, mutexes and files all share the handle type, as they do on
	// Windows, so each kind is recognised before the plain-descriptor case.
	if (auto* state = pob_win_compat::AsEvent(h)) {
		delete state;
		return 1;
	}
	// A named mutex exists while some handle to it does; closing the last one
	// has to drop the lock that stands for that, or the object is reported as
	// held for the rest of the process's life.
	if (pob_win_compat::CloseMutexHandle(h)) {
		return 1;
	}
	const int fd = pob_win_compat::HandleToFd(h);
	if (fd < 0) return 0;
	return ::close(fd) == 0 ? 1 : 0;
}

inline int FlushFileBuffers(HANDLE h)
{
	const int fd = pob_win_compat::HandleToFd(h);
	if (fd < 0) return 0;
	// A pipe has nothing to sync and reports EINVAL; the bytes are already with
	// the kernel either way, so that is not a failure worth propagating.
	return (::fsync(fd) == 0 || errno == EINVAL) ? 1 : 0;
}

inline int CreateDirectoryW(const wchar_t* path, void* security)
{
	(void)security;
	if (path == nullptr) return 0;
	const std::string utf8 = pob_win_compat::WideToPath(path);
	return ::mkdir(utf8.c_str(), 0755) == 0 ? 1 : 0;
}

inline int CreateDirectoryA(const char* path, void* security)
{
	(void)security;
	if (path == nullptr) return 0;
	return ::mkdir(pob_win_compat::NormalizePath(path).c_str(), 0755) == 0 ? 1 : 0;
}

// Win32: the path of the running executable, wide form.
inline unsigned long GetModuleFileNameW(void* module, wchar_t* buffer, unsigned long size)
{
	(void)module;
	if (buffer == nullptr || size == 0) return 0;
	char narrow[4096] = {};
	if (GetModuleFileNameA(nullptr, narrow, sizeof(narrow)) == 0) {
		buffer[0] = L'\0';
		return 0;
	}
	return static_cast<unsigned long>(pob_win_compat::Utf8ToWide(narrow, buffer, size));
}

inline unsigned long GetCurrentProcessId()
{
	return static_cast<unsigned long>(::getpid());
}

// ----------------------------------------------------- error codes
//
// Declared here, ahead of GetLastError, because that function translates into
// them. The later block repeats the ones it needs under the same guards.

#if !defined(ERROR_SUCCESS)
#define ERROR_SUCCESS 0u
#endif
#if !defined(ERROR_FILE_NOT_FOUND)
#define ERROR_FILE_NOT_FOUND 2u
#endif
#if !defined(ERROR_PATH_NOT_FOUND)
#define ERROR_PATH_NOT_FOUND 3u
#endif
#if !defined(ERROR_ACCESS_DENIED)
#define ERROR_ACCESS_DENIED 5u
#endif
#if !defined(ERROR_SHARING_VIOLATION)
#define ERROR_SHARING_VIOLATION 32u
#endif
#if !defined(ERROR_FILE_EXISTS)
#define ERROR_FILE_EXISTS 80u
#endif
#if !defined(ERROR_INSUFFICIENT_BUFFER)
#define ERROR_INSUFFICIENT_BUFFER 122u
#endif
#if !defined(ERROR_ALREADY_EXISTS)
#define ERROR_ALREADY_EXISTS 183u
#endif

// Win32: the last error of the calling thread.
//
// Callers compare this against Win32 codes -- ERROR_ALREADY_EXISTS and the
// like -- whose numbers have nothing to do with errno's. Returning errno raw
// made every one of those comparisons fail silently. The recursive mkdir
// helper is the clearest case: a level that already existed came back as an
// unrecognised error, the helper treated it as fatal, and nothing below that
// level was ever created. Everything downstream then operated on directories
// that were not there.
//
// Only the codes call sites actually compare against are translated; anything
// else keeps its errno value, which says more than a wrong Win32 code would.
inline unsigned long GetLastError()
{
	switch (errno) {
	case 0:
		return ERROR_SUCCESS;
	case ENOENT:
		return ERROR_FILE_NOT_FOUND;
	case ENOTDIR:
		return ERROR_PATH_NOT_FOUND;
	case EEXIST:
		return ERROR_ALREADY_EXISTS;
	case EACCES:
	case EPERM:
	case EROFS:
		return ERROR_ACCESS_DENIED;
	case EAGAIN: // EWOULDBLOCK is the same value here
		return ERROR_SHARING_VIOLATION;
	case ENAMETOOLONG:
	case ERANGE:
		return ERROR_INSUFFICIENT_BUFFER;
	default:
		break;
	}
	return static_cast<unsigned long>(errno);
}

// ----------------------------------------------------------------- time

// Win32: broken-down local time. Field names and the 1-based month match the
// original so call sites format identically.
struct SYSTEMTIME {
	unsigned short wYear;
	unsigned short wMonth;
	unsigned short wDayOfWeek;
	unsigned short wDay;
	unsigned short wHour;
	unsigned short wMinute;
	unsigned short wSecond;
	unsigned short wMilliseconds;
};

inline void GetLocalTime(SYSTEMTIME* out)
{
	if (out == nullptr) return;
	timespec ts = {};
	::clock_gettime(CLOCK_REALTIME, &ts);
	tm local = {};
	::localtime_r(&ts.tv_sec, &local);
	out->wYear = static_cast<unsigned short>(local.tm_year + 1900);
	out->wMonth = static_cast<unsigned short>(local.tm_mon + 1);
	out->wDayOfWeek = static_cast<unsigned short>(local.tm_wday);
	out->wDay = static_cast<unsigned short>(local.tm_mday);
	out->wHour = static_cast<unsigned short>(local.tm_hour);
	out->wMinute = static_cast<unsigned short>(local.tm_min);
	out->wSecond = static_cast<unsigned short>(local.tm_sec);
	out->wMilliseconds = static_cast<unsigned short>(ts.tv_nsec / 1000000);
}

inline void GetSystemTime(SYSTEMTIME* out)
{
	if (out == nullptr) return;
	timespec ts = {};
	::clock_gettime(CLOCK_REALTIME, &ts);
	tm utc = {};
	::gmtime_r(&ts.tv_sec, &utc);
	out->wYear = static_cast<unsigned short>(utc.tm_year + 1900);
	out->wMonth = static_cast<unsigned short>(utc.tm_mon + 1);
	out->wDayOfWeek = static_cast<unsigned short>(utc.tm_wday);
	out->wDay = static_cast<unsigned short>(utc.tm_mday);
	out->wHour = static_cast<unsigned short>(utc.tm_hour);
	out->wMinute = static_cast<unsigned short>(utc.tm_min);
	out->wSecond = static_cast<unsigned short>(utc.tm_sec);
	out->wMilliseconds = static_cast<unsigned short>(ts.tv_nsec / 1000000);
}

// Win32: a 64-bit count of 100 ns ticks since 1601-01-01, split in two halves.
// These three carry a 64-bit value as two halves, and callers reach for the
// halves and the whole interchangeably. The halves must therefore be exactly
// 32 bits: `unsigned long` is 32 bits on Windows but 64 here, which would make
// the struct arm 16 bytes against an 8-byte QuadPart, so a value written as
// halves read back as its low half alone. The log retention sweep did exactly
// that: its cutoff date came out tens of thousands of years in the future and
// it deleted today's log.
struct FILETIME {
	uint32_t dwLowDateTime;
	uint32_t dwHighDateTime;
};

union ULARGE_INTEGER {
	struct { uint32_t LowPart; uint32_t HighPart; };
	unsigned long long QuadPart;
};

namespace pob_win_compat {

// Seconds between the Win32 epoch (1601-01-01) and the Unix epoch.
constexpr unsigned long long kEpochDeltaSeconds = 11644473600ULL;

inline void PackFileTime(unsigned long long ticks, FILETIME* out)
{
	if (out == nullptr) return;
	out->dwLowDateTime = static_cast<unsigned long>(ticks & 0xFFFFFFFFULL);
	out->dwHighDateTime = static_cast<unsigned long>(ticks >> 32);
}

inline unsigned long long UnixToFileTimeTicks(long long seconds, long nanoseconds)
{
	return (static_cast<unsigned long long>(seconds) + kEpochDeltaSeconds) * 10000000ULL
	     + static_cast<unsigned long long>(nanoseconds) / 100ULL;
}

} // namespace pob_win_compat

inline void GetSystemTimeAsFileTime(FILETIME* out)
{
	timespec ts = {};
	::clock_gettime(CLOCK_REALTIME, &ts);
	pob_win_compat::PackFileTime(
		pob_win_compat::UnixToFileTimeTicks(ts.tv_sec, ts.tv_nsec), out);
}

inline HANDLE GetCurrentProcess()
{
	// A pseudo-handle on Windows; here it only ever reaches GetProcessTimes
	// below, which asks the kernel about this process directly.
	return reinterpret_cast<HANDLE>(static_cast<intptr_t>(-2));
}

// Win32: the four timestamps of a process. The engine reads the creation time
// only, to report elapsed time since launch; the kernel and user totals are
// zeroed rather than approximated from an unrelated clock.
inline int GetProcessTimes(HANDLE process, FILETIME* creation, FILETIME* exit,
                           FILETIME* kernel, FILETIME* user)
{
	(void)process;
	if (exit != nullptr) pob_win_compat::PackFileTime(0, exit);

	// Kernel and user totals come from the process resource usage, in the same
	// 100 ns units the Win32 fields use, so callers computing a CPU percentage
	// from the delta get a real number rather than a flat zero.
	if (kernel != nullptr || user != nullptr) {
		rusage usage = {};
		const bool ok = ::getrusage(RUSAGE_SELF, &usage) == 0;
		if (kernel != nullptr) {
			const unsigned long long ticks = ok
				? static_cast<unsigned long long>(usage.ru_stime.tv_sec) * 10000000ULL
				  + static_cast<unsigned long long>(usage.ru_stime.tv_usec) * 10ULL
				: 0ULL;
			pob_win_compat::PackFileTime(ticks, kernel);
		}
		if (user != nullptr) {
			const unsigned long long ticks = ok
				? static_cast<unsigned long long>(usage.ru_utime.tv_sec) * 10000000ULL
				  + static_cast<unsigned long long>(usage.ru_utime.tv_usec) * 10ULL
				: 0ULL;
			pob_win_compat::PackFileTime(ticks, user);
		}
	}
	if (creation == nullptr) return 1;

#if defined(__APPLE__)
	kinfo_proc info = {};
	size_t len = sizeof(info);
	int mib[4] = { CTL_KERN, KERN_PROC, KERN_PROC_PID, ::getpid() };
	if (::sysctl(mib, 4, &info, &len, nullptr, 0) != 0 || len == 0) {
		return 0;
	}
	pob_win_compat::PackFileTime(
		pob_win_compat::UnixToFileTimeTicks(info.kp_proc.p_starttime.tv_sec,
		                                    info.kp_proc.p_starttime.tv_usec * 1000),
		creation);
	return 1;
#else
	// /proc/self/stat field 22 is the start time in clock ticks since boot.
	return 0;
#endif
}

// ---------------------------------------------------- code page conversion

#if !defined(CP_UTF8)
#define CP_UTF8 65001u
#endif
#if !defined(CP_ACP)
#define CP_ACP 0u
#endif

// Win32: wide text to a byte encoding. Only UTF-8 is requested here, which is
// what the helper above produces. A null or zero-length output buffer means
// "tell me the size", as on Windows; the returned count includes the
// terminator only when the input length was given as -1.
inline int WideCharToMultiByte(unsigned int codePage, unsigned long flags,
                               const wchar_t* input, int inputLen,
                               char* output, int outputSize,
                               const char* defaultChar, int* usedDefault)
{
	(void)codePage; (void)flags; (void)defaultChar;
	if (usedDefault != nullptr) *usedDefault = 0;
	if (input == nullptr) return 0;

	std::string utf8;
	if (inputLen < 0) {
		utf8 = pob_win_compat::WideToUtf8(input);
		utf8.push_back('\0'); // -1 means the input was terminated; so is the output
	} else {
		const std::wstring slice(input, static_cast<size_t>(inputLen));
		utf8 = pob_win_compat::WideToUtf8(slice.c_str());
	}
	if (output == nullptr || outputSize == 0) {
		return static_cast<int>(utf8.size());
	}
	if (utf8.size() > static_cast<size_t>(outputSize)) return 0;
	std::memcpy(output, utf8.data(), utf8.size());
	return static_cast<int>(utf8.size());
}

// Win32: the other direction, with the same size-query convention.
inline int MultiByteToWideChar(unsigned int codePage, unsigned long flags,
                              const char* input, int inputLen,
                              wchar_t* output, int outputSize)
{
	(void)codePage; (void)flags;
	if (input == nullptr) return 0;
	const std::string slice = (inputLen < 0)
		? std::string(input)
		: std::string(input, static_cast<size_t>(inputLen));

	// Convert into a buffer of our own first. Utf8ToWide always writes a
	// terminator and reserves room for it, so handing it the caller's buffer
	// directly costs one character: with an explicit input length Win32 fills
	// the buffer completely and adds no terminator, so a caller that sized the
	// buffer exactly -- the usual two-call pattern -- lost the last character
	// of every string. File names came out as "Launch.lu".
	std::wstring wide(slice.size() + 1, L'\0');
	wide.resize(pob_win_compat::Utf8ToWide(slice.c_str(), &wide[0], wide.size()));

	// Win32 counts the terminator only when the input was given terminated.
	const size_t needed = wide.size() + (inputLen < 0 ? 1 : 0);
	if (output == nullptr || outputSize == 0)
		return static_cast<int>(needed);
	if (needed > static_cast<size_t>(outputSize)) return 0;
	if (!wide.empty())
		std::memcpy(output, wide.data(), wide.size() * sizeof(wchar_t));
	if (inputLen < 0) output[wide.size()] = L'\0';
	return static_cast<int>(needed);
}

// ------------------------------------------- performance counter / misc

// Win32: a signed 64-bit value split in two halves.
union LARGE_INTEGER {
	struct { uint32_t LowPart; int32_t HighPart; };
	long long QuadPart;
};

// Win32: the attribute bits of a wide path. Same contract as the narrow form.
inline unsigned long GetFileAttributesW(const wchar_t* path)
{
	if (path == nullptr) return INVALID_FILE_ATTRIBUTES;
	const std::string utf8 = pob_win_compat::WideToPath(path);
	return GetFileAttributesA(utf8.c_str());
}

inline int SetFileAttributesW(const wchar_t* path, unsigned long attributes)
{
	// Win32 attributes have no POSIX counterpart worth emulating -- the one
	// use here is clearing the read-only bit, which chmod covers.
	if (path == nullptr) return 0;
	const std::string utf8 = pob_win_compat::WideToPath(path);
	struct stat st = {};
	if (::stat(utf8.c_str(), &st) != 0) return 0;
	const mode_t writable = st.st_mode | S_IWUSR;
	const mode_t readonly = st.st_mode & ~static_cast<mode_t>(S_IWUSR | S_IWGRP | S_IWOTH);
	return ::chmod(utf8.c_str(), (attributes & 0x1u) ? readonly : writable) == 0 ? 1 : 0;
}

// Win32: the size of an open file, reported through a 64-bit out-parameter.
inline int GetFileSizeEx(HANDLE h, LARGE_INTEGER* size)
{
	if (size == nullptr) return 0;
	const int fd = pob_win_compat::HandleToFd(h);
	if (fd < 0) return 0;
	struct stat st = {};
	if (::fstat(fd, &st) != 0) return 0;
	size->QuadPart = static_cast<long long>(st.st_size);
	return 1;
}

// Win32: a monotonic tick count and its rate. The monotonic clock reports
// nanoseconds, so the rate is fixed and the counter needs no calibration.
inline int QueryPerformanceCounter(LARGE_INTEGER* out)
{
	if (out == nullptr) return 0;
	timespec ts = {};
	::clock_gettime(CLOCK_MONOTONIC, &ts);
	out->QuadPart = static_cast<long long>(ts.tv_sec) * 1000000000LL + ts.tv_nsec;
	return 1;
}

inline int QueryPerformanceFrequency(LARGE_INTEGER* out)
{
	if (out == nullptr) return 0;
	out->QuadPart = 1000000000LL; // the counter above is in nanoseconds
	return 1;
}

inline int DeleteFileW(const wchar_t* path)
{
	if (path == nullptr) return 0;
	const std::string utf8 = pob_win_compat::WideToPath(path);
	return ::unlink(utf8.c_str()) == 0 ? 1 : 0;
}

inline int DeleteFileA(const char* path)
{
	if (path == nullptr) return 0;
	return ::unlink(pob_win_compat::NormalizePath(path).c_str()) == 0 ? 1 : 0;
}

// Win32: converts between the two time representations. Both directions go
// through the Unix epoch, which is the only shared reference available.
inline int SystemTimeToFileTime(const SYSTEMTIME* in, FILETIME* out)
{
	if (in == nullptr || out == nullptr) return 0;
	tm parts = {};
	parts.tm_year = in->wYear - 1900;
	parts.tm_mon = in->wMonth - 1;
	parts.tm_mday = in->wDay;
	parts.tm_hour = in->wHour;
	parts.tm_min = in->wMinute;
	parts.tm_sec = in->wSecond;
	parts.tm_isdst = -1;
	const time_t seconds = ::mktime(&parts);
	if (seconds == static_cast<time_t>(-1)) return 0;
	pob_win_compat::PackFileTime(
		pob_win_compat::UnixToFileTimeTicks(seconds, in->wMilliseconds * 1000000L), out);
	return 1;
}

inline int FileTimeToSystemTime(const FILETIME* in, SYSTEMTIME* out)
{
	if (in == nullptr || out == nullptr) return 0;
	const unsigned long long ticks =
		(static_cast<unsigned long long>(in->dwHighDateTime) << 32) | in->dwLowDateTime;
	const unsigned long long totalSeconds = ticks / 10000000ULL;
	if (totalSeconds < pob_win_compat::kEpochDeltaSeconds) return 0;
	const time_t seconds =
		static_cast<time_t>(totalSeconds - pob_win_compat::kEpochDeltaSeconds);
	tm local = {};
	if (::localtime_r(&seconds, &local) == nullptr) return 0;
	out->wYear = static_cast<unsigned short>(local.tm_year + 1900);
	out->wMonth = static_cast<unsigned short>(local.tm_mon + 1);
	out->wDayOfWeek = static_cast<unsigned short>(local.tm_wday);
	out->wDay = static_cast<unsigned short>(local.tm_mday);
	out->wHour = static_cast<unsigned short>(local.tm_hour);
	out->wMinute = static_cast<unsigned short>(local.tm_min);
	out->wSecond = static_cast<unsigned short>(local.tm_sec);
	out->wMilliseconds = static_cast<unsigned short>((ticks / 10000ULL) % 1000ULL);
	return 1;
}

// ------------------------------------------------------ directory listing

// Win32: one entry of a directory walk. Only the two fields the engine reads
// are provided; the rest of the Win32 structure is omitted rather than filled
// with values that would be guesses.
struct WIN32_FIND_DATAW {
	uint32_t dwFileAttributes;
	FILETIME ftCreationTime;
	FILETIME ftLastAccessTime;
	FILETIME ftLastWriteTime;
	uint32_t nFileSizeHigh;
	uint32_t nFileSizeLow;
	uint32_t dwReserved0;      // the reparse tag, when the entry is one
	uint32_t dwReserved1;
	wchar_t cFileName[MAX_PATH];
	wchar_t cAlternateFileName[14];
};

namespace pob_win_compat {

// Backing state for a FindFirstFileW walk: the open directory plus the
// filename glob split off the caller's path.
struct FindState {
	DIR* dir = nullptr;
	std::string pattern;
	// Kept so every entry, not just the first, can be described in full: the
	// size and the timestamps need the path, not only the name.
	std::string dirPath;
};

inline bool FillFindData(const std::string& dirPath, const dirent* entry, WIN32_FIND_DATAW* out)
{
	std::string full = dirPath;
	if (!full.empty() && full.back() != '/') full.push_back('/');
	full += entry->d_name;

	struct stat st = {};
	*out = WIN32_FIND_DATAW{};
	out->dwFileAttributes = FILE_ATTRIBUTE_NORMAL;
	if (::lstat(full.c_str(), &st) == 0) {
		if (S_ISDIR(st.st_mode)) out->dwFileAttributes = FILE_ATTRIBUTE_DIRECTORY;
		if (S_ISLNK(st.st_mode)) {
			// A symlink is the closest thing to a reparse point, and the
			// reparse tag is what the caller checks to recognise one.
			out->dwFileAttributes |= FILE_ATTRIBUTE_REPARSE_POINT;
			out->dwReserved0 = IO_REPARSE_TAG_SYMLINK;
		}
		out->nFileSizeHigh = static_cast<unsigned long>(static_cast<unsigned long long>(st.st_size) >> 32);
		out->nFileSizeLow = static_cast<unsigned long>(static_cast<unsigned long long>(st.st_size) & 0xFFFFFFFFULL);
		PackFileTime(UnixToFileTimeTicks(st.st_mtime, 0), &out->ftLastWriteTime);
		PackFileTime(UnixToFileTimeTicks(st.st_atime, 0), &out->ftLastAccessTime);
		PackFileTime(UnixToFileTimeTicks(st.st_ctime, 0), &out->ftCreationTime);
	}
	return Utf8ToWide(entry->d_name, out->cFileName, MAX_PATH) > 0;
}

} // namespace pob_win_compat

// Win32: begins a directory walk matching a path that ends in a glob.
inline HANDLE FindFirstFileW(const wchar_t* pathWithGlob, WIN32_FIND_DATAW* out)
{
	if (pathWithGlob == nullptr || out == nullptr) return INVALID_HANDLE_VALUE;
	const std::string full = pob_win_compat::WideToPath(pathWithGlob);
	const size_t slash = full.find_last_of("/\\");
	const std::string dirPath = (slash == std::string::npos) ? std::string(".") : full.substr(0, slash);
	const std::string pattern = (slash == std::string::npos) ? full : full.substr(slash + 1);

	DIR* dir = ::opendir(dirPath.empty() ? "." : dirPath.c_str());
	if (dir == nullptr) return INVALID_HANDLE_VALUE;

	auto* state = new pob_win_compat::FindState{ dir, pattern, dirPath };
	for (dirent* entry = ::readdir(dir); entry != nullptr; entry = ::readdir(dir)) {
		if (::fnmatch(state->pattern.c_str(), entry->d_name, 0) != 0) continue;
		if (pob_win_compat::FillFindData(dirPath, entry, out)) {
			return reinterpret_cast<HANDLE>(state);
		}
	}
	::closedir(dir);
	delete state;
	return INVALID_HANDLE_VALUE;
}

inline int FindNextFileW(HANDLE h, WIN32_FIND_DATAW* out)
{
	if (h == INVALID_HANDLE_VALUE || h == nullptr || out == nullptr) return 0;
	auto* state = reinterpret_cast<pob_win_compat::FindState*>(h);
	// Described exactly as the first entry was. Filling in only the name and
	// the directory bit here, as this once did, left the size and the
	// timestamps at zero for every entry after the first -- a caller that
	// prunes by age then reads them all as 1601 and deletes the lot.
	for (dirent* entry = ::readdir(state->dir); entry != nullptr; entry = ::readdir(state->dir)) {
		if (::fnmatch(state->pattern.c_str(), entry->d_name, 0) != 0) continue;
		if (pob_win_compat::FillFindData(state->dirPath, entry, out)) return 1;
	}
	return 0;
}

inline int FindClose(HANDLE h)
{
	if (h == INVALID_HANDLE_VALUE || h == nullptr) return 0;
	auto* state = reinterpret_cast<pob_win_compat::FindState*>(h);
	if (state->dir != nullptr) ::closedir(state->dir);
	delete state;
	return 1;
}

// ------------------------------------------------- more file operations

#if !defined(MOVEFILE_REPLACE_EXISTING)
#define MOVEFILE_REPLACE_EXISTING 0x00000001u
#endif
#if !defined(MOVEFILE_WRITE_THROUGH)
#define MOVEFILE_WRITE_THROUGH 0x00000008u
#endif
#if !defined(MOVEFILE_COPY_ALLOWED)
#define MOVEFILE_COPY_ALLOWED 0x00000002u
#endif

inline int RemoveDirectoryW(const wchar_t* path)
{
	if (path == nullptr) return 0;
	const std::string utf8 = pob_win_compat::WideToPath(path);
	return ::rmdir(utf8.c_str()) == 0 ? 1 : 0;
}

inline int RemoveDirectoryA(const char* path)
{
	if (path == nullptr) return 0;
	return ::rmdir(pob_win_compat::NormalizePath(path).c_str()) == 0 ? 1 : 0;
}

// Win32: the per-user temp directory, with a trailing separator. TMPDIR is the
// POSIX equivalent; /tmp is the fallback when it is unset.
inline unsigned long GetTempPathW(unsigned long size, wchar_t* buffer)
{
	const char* tmp = std::getenv("TMPDIR");
	std::string path = (tmp != nullptr && tmp[0] != 0) ? tmp : "/tmp";
	if (path.back() != '/') path.push_back('/');
	if (buffer == nullptr || path.size() + 1 > size) {
		return static_cast<unsigned long>(path.size() + 1);
	}
	return static_cast<unsigned long>(pob_win_compat::Utf8ToWide(path.c_str(), buffer, size));
}

inline unsigned long GetTempPathA(unsigned long size, char* buffer)
{
	const char* tmp = std::getenv("TMPDIR");
	std::string path = (tmp != nullptr && tmp[0] != 0) ? tmp : "/tmp";
	if (path.back() != '/') path.push_back('/');
	if (buffer == nullptr || path.size() + 1 > size) {
		return static_cast<unsigned long>(path.size() + 1);
	}
	std::memcpy(buffer, path.c_str(), path.size() + 1);
	return static_cast<unsigned long>(path.size());
}

// Win32: moves or renames, replacing the target when asked. rename() already
// replaces, so the flag only decides whether an existing target is an error.
inline int MoveFileExW(const wchar_t* from, const wchar_t* to, unsigned long flags)
{
	if (from == nullptr || to == nullptr) return 0;
	const std::string a = pob_win_compat::WideToPath(from);
	const std::string b = pob_win_compat::WideToPath(to);
	if ((flags & MOVEFILE_REPLACE_EXISTING) == 0) {
		struct stat st = {};
		if (::stat(b.c_str(), &st) == 0) return 0; // target exists, no replace asked
	}
	return ::rename(a.c_str(), b.c_str()) == 0 ? 1 : 0;
}

inline int MoveFileW(const wchar_t* from, const wchar_t* to)
{
	return MoveFileExW(from, to, 0);
}

// Win32: copies a file, optionally failing when the target exists.
inline int CopyFileW(const wchar_t* from, const wchar_t* to, int failIfExists)
{
	if (from == nullptr || to == nullptr) return 0;
	const std::string a = pob_win_compat::WideToPath(from);
	const std::string b = pob_win_compat::WideToPath(to);
	if (failIfExists) {
		struct stat st = {};
		if (::stat(b.c_str(), &st) == 0) return 0;
	}
	const int in = ::open(a.c_str(), O_RDONLY);
	if (in < 0) return 0;
	struct stat src = {};
	const mode_t mode = (::fstat(in, &src) == 0) ? (src.st_mode & 0777) : 0644;
	const int out = ::open(b.c_str(), O_WRONLY | O_CREAT | O_TRUNC, mode);
	if (out < 0) { ::close(in); return 0; }
	char buf[65536];
	bool ok = true;
	for (;;) {
		const ssize_t n = ::read(in, buf, sizeof(buf));
		if (n < 0) { if (errno == EINTR) continue; ok = false; break; }
		if (n == 0) break;
		const char* p = buf;
		ssize_t left = n;
		while (left > 0) {
			const ssize_t w = ::write(out, p, static_cast<size_t>(left));
			if (w < 0) { if (errno == EINTR) continue; ok = false; break; }
			p += w; left -= w;
		}
		if (!ok) break;
	}
	::close(in);
	::close(out);
	if (!ok) ::unlink(b.c_str());
	return ok ? 1 : 0;
}

// Win32 (Shell): creates a directory and every missing parent.
inline int SHCreateDirectoryExW(void* parentWindow, const wchar_t* path, void* security)
{
	(void)parentWindow; (void)security;
	if (path == nullptr) return -1;
	const std::string utf8 = pob_win_compat::WideToPath(path);
	std::string sofar;
	for (size_t i = 0; i <= utf8.size(); i++) {
		if (i == utf8.size() || utf8[i] == '/') {
			if (!sofar.empty() && sofar != "/") {
				struct stat st = {};
				if (::stat(sofar.c_str(), &st) != 0 && ::mkdir(sofar.c_str(), 0755) != 0 && errno != EEXIST) {
					return -1;
				}
			}
		}
		if (i < utf8.size()) sofar.push_back(utf8[i]);
	}
	return 0; // ERROR_SUCCESS
}

// Win32: the attributes-plus-size-plus-times form of a stat.
#if !defined(GetFileExInfoStandard)
#define GetFileExInfoStandard 0
#endif

struct WIN32_FILE_ATTRIBUTE_DATA {
	uint32_t dwFileAttributes;
	FILETIME ftCreationTime;
	FILETIME ftLastAccessTime;
	FILETIME ftLastWriteTime;
	uint32_t nFileSizeHigh;
	uint32_t nFileSizeLow;
};

inline int GetFileAttributesExW(const wchar_t* path, int level, void* out)
{
	(void)level;
	if (path == nullptr || out == nullptr) return 0;
	const std::string utf8 = pob_win_compat::WideToPath(path);
	struct stat st = {};
	if (::stat(utf8.c_str(), &st) != 0) return 0;
	auto* data = static_cast<WIN32_FILE_ATTRIBUTE_DATA*>(out);
	data->dwFileAttributes = S_ISDIR(st.st_mode) ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL;
	const unsigned long long ticks = pob_win_compat::UnixToFileTimeTicks(st.st_mtime, 0);
	pob_win_compat::PackFileTime(ticks, &data->ftLastWriteTime);
	pob_win_compat::PackFileTime(pob_win_compat::UnixToFileTimeTicks(st.st_atime, 0), &data->ftLastAccessTime);
	pob_win_compat::PackFileTime(pob_win_compat::UnixToFileTimeTicks(st.st_ctime, 0), &data->ftCreationTime);
	data->nFileSizeHigh = static_cast<unsigned long>(static_cast<unsigned long long>(st.st_size) >> 32);
	data->nFileSizeLow = static_cast<unsigned long>(static_cast<unsigned long long>(st.st_size) & 0xFFFFFFFFULL);
	return 1;
}

inline int GetFileTime(HANDLE h, FILETIME* creation, FILETIME* access, FILETIME* write)
{
	const int fd = pob_win_compat::HandleToFd(h);
	if (fd < 0) return 0;
	struct stat st = {};
	if (::fstat(fd, &st) != 0) return 0;
	if (creation != nullptr) pob_win_compat::PackFileTime(pob_win_compat::UnixToFileTimeTicks(st.st_ctime, 0), creation);
	if (access != nullptr)   pob_win_compat::PackFileTime(pob_win_compat::UnixToFileTimeTicks(st.st_atime, 0), access);
	if (write != nullptr)    pob_win_compat::PackFileTime(pob_win_compat::UnixToFileTimeTicks(st.st_mtime, 0), write);
	return 1;
}

inline int SetFilePointerEx(HANDLE h, LARGE_INTEGER distance, LARGE_INTEGER* newPosition, unsigned long method)
{
	const int fd = pob_win_compat::HandleToFd(h);
	if (fd < 0) return 0;
	const int whence = (method == 1) ? SEEK_CUR : (method == 2) ? SEEK_END : SEEK_SET;
	const off_t pos = ::lseek(fd, static_cast<off_t>(distance.QuadPart), whence);
	if (pos == static_cast<off_t>(-1)) return 0;
	if (newPosition != nullptr) newPosition->QuadPart = static_cast<long long>(pos);
	return 1;
}

inline unsigned long GetFullPathNameW(const wchar_t* path, unsigned long size, wchar_t* buffer, wchar_t** filePart)
{
	if (filePart != nullptr) *filePart = nullptr;
	if (path == nullptr || buffer == nullptr) return 0;

	// Win32 resolves this LEXICALLY: it collapses "." and ".." and does not
	// require the path to exist. Callers depend on both halves of that -- the
	// containment check in modern_ui_shared.cpp resolves ".." precisely so a
	// prefix compare cannot be walked out of, and it runs against folders that
	// may not have been created yet.
	//
	// realpath would be the obvious counterpart and is the wrong one: it fails
	// on a missing path, which would leave ".." in place and defeat that check.
	// filesystem::path::lexically_normal does what Win32 does.
	std::filesystem::path p(pob_win_compat::NormalizePath(pob_win_compat::WideToUtf8(path)));
	if (p.is_relative()) {
		std::error_code ec;
		const std::filesystem::path cwd = std::filesystem::current_path(ec);
		if (!ec) p = cwd / p;
	}
	const std::string full = p.lexically_normal().string();
	if (full.size() + 1 > size) {
		return static_cast<unsigned long>(full.size() + 1);
	}
	return static_cast<unsigned long>(pob_win_compat::Utf8ToWide(full.c_str(), buffer, size));
}

inline unsigned long GetCurrentDirectoryW(unsigned long size, wchar_t* buffer)
{
	char cwd[4096] = {};
	if (::getcwd(cwd, sizeof(cwd)) == nullptr) return 0;
	if (buffer == nullptr) return static_cast<unsigned long>(std::strlen(cwd) + 1);
	return static_cast<unsigned long>(pob_win_compat::Utf8ToWide(cwd, buffer, size));
}

inline int SetEnvironmentVariableA(const char* name, const char* value)
{
	if (name == nullptr) return 0;
	if (value == nullptr) return ::unsetenv(name) == 0 ? 1 : 0;
	return ::setenv(name, value, 1) == 0 ? 1 : 0;
}

inline int SetEnvironmentVariableW(const wchar_t* name, const wchar_t* value)
{
	if (name == nullptr) return 0;
	const std::string n = pob_win_compat::WideToUtf8(name);
	if (value == nullptr) return ::unsetenv(n.c_str()) == 0 ? 1 : 0;
	const std::string v = pob_win_compat::WideToUtf8(value);
	return ::setenv(n.c_str(), v.c_str(), 1) == 0 ? 1 : 0;
}

// ------------------------------------------- MSVC secure-CRT equivalents

inline int freopen_s(FILE** out, const char* path, const char* mode, FILE* stream)
{
	if (out == nullptr) return EINVAL;

	// CONOUT$ / CONIN$ are Windows console device names, not files: a GUI
	// process reopens its streams onto them after AttachConsole so its output
	// appears in the parent's console. A POSIX process already has those
	// streams, so there is nothing to reopen -- and treating the name as a
	// file would silently redirect every diagnostic into a file called
	// "CONOUT$", which is exactly what happened before this check existed.
	if (path != nullptr) {
		const bool isConsoleDevice =
			std::strcmp(path, "CONOUT$") == 0 ||
			std::strcmp(path, "CONIN$") == 0 ||
			std::strcmp(path, "CONERR$") == 0;
		if (isConsoleDevice) {
			*out = stream;
			return 0;
		}
	}

	*out = std::freopen(pob_win_compat::NormalizePath(path).c_str(), mode, stream);
	return *out != nullptr ? 0 : (errno != 0 ? errno : EINVAL);
}

inline int _wfopen_s(FILE** out, const wchar_t* path, const wchar_t* mode)
{
	if (out == nullptr) return EINVAL;
	const std::string p = pob_win_compat::WideToPath(path);
	const std::string m = pob_win_compat::WideToUtf8(mode);
	*out = std::fopen(p.c_str(), m.c_str());
	return *out != nullptr ? 0 : (errno != 0 ? errno : EINVAL);
}

inline FILE* _wfopen(const wchar_t* path, const wchar_t* mode)
{
	const std::string p = pob_win_compat::WideToPath(path);
	const std::string m = pob_win_compat::WideToUtf8(mode);
	return std::fopen(p.c_str(), m.c_str());
}

inline int _wcsicmp(const wchar_t* a, const wchar_t* b)
{
	// No portable wide case-insensitive compare; the callers use it on ASCII
	// file names and keys, which this handles exactly.
	for (; *a != 0 && *b != 0; ++a, ++b) {
		const wchar_t ca = (*a >= L'A' && *a <= L'Z') ? *a + 32 : *a;
		const wchar_t cb = (*b >= L'A' && *b <= L'Z') ? *b + 32 : *b;
		if (ca != cb) return ca < cb ? -1 : 1;
	}
	if (*a == *b) return 0;
	return *a == 0 ? -1 : 1;
}

// ----------------------------------------------------------- console

// Win32: a GUI process can attach to the parent's console to print there.
// A POSIX process already inherits stdout, so there is nothing to attach.
#if !defined(ATTACH_PARENT_PROCESS)
#define ATTACH_PARENT_PROCESS 0xFFFFFFFFu
#endif

inline int AttachConsole(unsigned long processId) { (void)processId; return 1; }
inline int AllocConsole() { return 1; }
inline int FreeConsole() { return 1; }
inline int SetConsoleOutputCP(unsigned int codePage) { (void)codePage; return 1; }

// ----------------------------------------------------- error codes

#if !defined(ERROR_SUCCESS)
#define ERROR_SUCCESS 0u
#endif
#if !defined(ERROR_FILE_NOT_FOUND)
#define ERROR_FILE_NOT_FOUND 2u
#endif
#if !defined(ERROR_PATH_NOT_FOUND)
#define ERROR_PATH_NOT_FOUND 3u
#endif
#if !defined(ERROR_ACCESS_DENIED)
#define ERROR_ACCESS_DENIED 5u
#endif
#if !defined(ERROR_ALREADY_EXISTS)
#define ERROR_ALREADY_EXISTS 183u
#endif
#if !defined(ERROR_INSUFFICIENT_BUFFER)
#define ERROR_INSUFFICIENT_BUFFER 122u
#endif

using PWSTR = wchar_t*;
using PCWSTR = const wchar_t*;
using PSTR = char*;
using PCSTR = const char*;
using LPWSTR_T = wchar_t*;

// -------------------------------------------------------- .ini files

// Win32 keeps small settings in .ini files through this API. There is no
// equivalent here, so the format is parsed directly: it is simple enough that
// matching the behaviour the callers depend on (section lookup, key lookup,
// default on miss, in-place update on write) is clearer than redirecting them
// to a different store, which would also strand the settings a Windows
// install already wrote.

namespace pob_win_compat {

inline std::string TrimCopy(const std::string& in)
{
	size_t a = in.find_first_not_of(" \t\r\n");
	if (a == std::string::npos) return std::string();
	size_t b = in.find_last_not_of(" \t\r\n");
	return in.substr(a, b - a + 1);
}

inline bool IniEqual(const std::string& a, const std::string& b)
{
	if (a.size() != b.size()) return false;
	for (size_t i = 0; i < a.size(); i++) {
		char ca = a[i], cb = b[i];
		if (ca >= 'A' && ca <= 'Z') ca = static_cast<char>(ca + 32);
		if (cb >= 'A' && cb <= 'Z') cb = static_cast<char>(cb + 32);
		if (ca != cb) return false;
	}
	return true;
}

inline std::string IniRead(const std::string& path)
{
	FILE* f = std::fopen(path.c_str(), "rb");
	if (f == nullptr) return std::string();
	std::string out;
	char buf[4096];
	size_t n;
	while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) out.append(buf, n);
	std::fclose(f);
	return out;
}

inline bool IniWrite(const std::string& path, const std::string& content)
{
	FILE* f = std::fopen(path.c_str(), "wb");
	if (f == nullptr) return false;
	const bool ok = content.empty() ||
		std::fwrite(content.data(), 1, content.size(), f) == content.size();
	std::fclose(f);
	return ok;
}

// Returns the value for section/key, or an empty string when absent.
inline std::string IniGet(const std::string& path, const std::string& section, const std::string& key)
{
	const std::string text = IniRead(path);
	std::string current;
	size_t pos = 0;
	while (pos <= text.size()) {
		size_t nl = text.find('\n', pos);
		if (nl == std::string::npos) nl = text.size();
		const std::string line = TrimCopy(text.substr(pos, nl - pos));
		pos = nl + 1;
		if (line.empty() || line[0] == ';' || line[0] == '#') continue;
		if (line.front() == '[' && line.back() == ']') {
			current = TrimCopy(line.substr(1, line.size() - 2));
			continue;
		}
		const size_t eq = line.find('=');
		if (eq == std::string::npos) continue;
		if (!IniEqual(current, section)) continue;
		if (IniEqual(TrimCopy(line.substr(0, eq)), key)) {
			return TrimCopy(line.substr(eq + 1));
		}
	}
	return std::string();
}

// Sets section/key, creating either when missing. A null value deletes the key.
inline bool IniSet(const std::string& path, const std::string& section,
                   const std::string& key, const char* value)
{
	const std::string text = IniRead(path);
	std::vector<std::string> lines;
	size_t pos = 0;
	while (pos <= text.size()) {
		size_t nl = text.find('\n', pos);
		if (nl == std::string::npos) nl = text.size();
		std::string line = text.substr(pos, nl - pos);
		if (!line.empty() && line.back() == '\r') line.pop_back();
		lines.push_back(line);
		if (nl == text.size()) break;
		pos = nl + 1;
	}

	int sectionStart = -1, sectionEnd = -1, keyLine = -1;
	std::string current;
	for (size_t i = 0; i < lines.size(); i++) {
		const std::string t = TrimCopy(lines[i]);
		if (t.size() >= 2 && t.front() == '[' && t.back() == ']') {
			const std::string name = TrimCopy(t.substr(1, t.size() - 2));
			if (IniEqual(name, section)) { sectionStart = static_cast<int>(i); sectionEnd = -1; }
			else if (sectionStart >= 0 && sectionEnd < 0) { sectionEnd = static_cast<int>(i); }
			continue;
		}
		if (sectionStart >= 0 && sectionEnd < 0 && keyLine < 0) {
			const size_t eq = t.find('=');
			if (eq != std::string::npos && IniEqual(TrimCopy(t.substr(0, eq)), key)) {
				keyLine = static_cast<int>(i);
			}
		}
	}
	if (sectionEnd < 0) sectionEnd = static_cast<int>(lines.size());

	if (value == nullptr) {
		if (keyLine >= 0) lines.erase(lines.begin() + keyLine);
	} else if (keyLine >= 0) {
		lines[keyLine] = key + "=" + value;
	} else if (sectionStart >= 0) {
		lines.insert(lines.begin() + sectionEnd, key + "=" + value);
	} else {
		if (!lines.empty() && !TrimCopy(lines.back()).empty()) lines.push_back(std::string());
		lines.push_back("[" + section + "]");
		lines.push_back(key + "=" + value);
	}

	std::string out;
	for (size_t i = 0; i < lines.size(); i++) {
		out += lines[i];
		if (i + 1 < lines.size()) out += "\n";
	}
	if (!out.empty() && out.back() != '\n') out += "\n";
	return IniWrite(path, out);
}

} // namespace pob_win_compat

inline unsigned long GetPrivateProfileStringW(const wchar_t* section, const wchar_t* key,
                                              const wchar_t* fallback, wchar_t* buffer,
                                              unsigned long size, const wchar_t* path)
{
	if (buffer == nullptr || size == 0) return 0;
	buffer[0] = L'\0';
	const std::string value = (section != nullptr && key != nullptr && path != nullptr)
		? pob_win_compat::IniGet(pob_win_compat::WideToPath(path),
		                         pob_win_compat::WideToUtf8(section),
		                         pob_win_compat::WideToUtf8(key))
		: std::string();
	if (!value.empty()) {
		return static_cast<unsigned long>(pob_win_compat::Utf8ToWide(value.c_str(), buffer, size));
	}
	if (fallback == nullptr) return 0;
	unsigned long n = 0;
	for (; fallback[n] != L'\0' && n + 1 < size; n++) buffer[n] = fallback[n];
	buffer[n] = L'\0';
	return n;
}

inline unsigned int GetPrivateProfileIntW(const wchar_t* section, const wchar_t* key,
                                          int fallback, const wchar_t* path)
{
	if (section == nullptr || key == nullptr || path == nullptr) {
		return static_cast<unsigned int>(fallback);
	}
	const std::string value = pob_win_compat::IniGet(pob_win_compat::WideToPath(path),
	                                                 pob_win_compat::WideToUtf8(section),
	                                                 pob_win_compat::WideToUtf8(key));
	if (value.empty()) return static_cast<unsigned int>(fallback);
	errno = 0;
	const long parsed = std::strtol(value.c_str(), nullptr, 10);
	return errno == 0 ? static_cast<unsigned int>(parsed) : static_cast<unsigned int>(fallback);
}

inline int WritePrivateProfileStringW(const wchar_t* section, const wchar_t* key,
                                      const wchar_t* value, const wchar_t* path)
{
	if (section == nullptr || path == nullptr) return 0;
	const std::string p = pob_win_compat::WideToPath(path);
	const std::string s = pob_win_compat::WideToUtf8(section);
	if (key == nullptr) return 0; // deleting a whole section is not used here
	const std::string k = pob_win_compat::WideToUtf8(key);
	if (value == nullptr) return pob_win_compat::IniSet(p, s, k, nullptr) ? 1 : 0;
	const std::string v = pob_win_compat::WideToUtf8(value);
	return pob_win_compat::IniSet(p, s, k, v.c_str()) ? 1 : 0;
}

// --------------------------------------------------- dynamic loading

inline HMODULE LoadLibraryExW(const wchar_t* path, HANDLE reserved, unsigned long flags)
{
	(void)reserved; (void)flags;
	if (path == nullptr) return nullptr;
	const std::string utf8 = pob_win_compat::WideToPath(path);
	return ::dlopen(utf8.c_str(), RTLD_LAZY);
}

inline HMODULE LoadLibraryW(const wchar_t* path)
{
	return LoadLibraryExW(path, nullptr, 0);
}

inline HMODULE GetModuleHandleW(const wchar_t* name)
{
	if (name == nullptr) return ::dlopen(nullptr, RTLD_LAZY | RTLD_NOLOAD);
	const std::string utf8 = pob_win_compat::WideToUtf8(name);
	return GetModuleHandleA(utf8.c_str());
}

inline void* GetProcAddress(HMODULE module, const char* name)
{
	if (module == nullptr || name == nullptr) return nullptr;
	return ::dlsym(module, name);
}

inline int FreeLibrary(HMODULE module)
{
	if (module == nullptr) return 0;
	return ::dlclose(module) == 0 ? 1 : 0;
}

// Win32: adds a directory to the DLL search path. The equivalent here is the
// rpath baked into each binary at link time, which is already set, so there is
// nothing for this to change.
inline int SetDllDirectoryW(const wchar_t* path) { (void)path; return 1; }

// ------------------------------------------------------------- misc

inline int _wtoi(const wchar_t* text)
{
	if (text == nullptr) return 0;
	const std::string utf8 = pob_win_compat::WideToUtf8(text);
	return std::atoi(utf8.c_str());
}

inline int SetFileTime(HANDLE h, const FILETIME* creation, const FILETIME* access, const FILETIME* write)
{
	(void)creation; // no POSIX equivalent of a creation time that can be set
	const int fd = pob_win_compat::HandleToFd(h);
	if (fd < 0) return 0;
	struct stat st = {};
	if (::fstat(fd, &st) != 0) return 0;

	auto to_timespec = [](const FILETIME* ft, time_t fallback) {
		timespec ts = {};
		if (ft == nullptr) { ts.tv_sec = fallback; return ts; }
		const unsigned long long ticks =
			(static_cast<unsigned long long>(ft->dwHighDateTime) << 32) | ft->dwLowDateTime;
		const unsigned long long secs = ticks / 10000000ULL;
		ts.tv_sec = static_cast<time_t>(secs > pob_win_compat::kEpochDeltaSeconds
			? secs - pob_win_compat::kEpochDeltaSeconds : 0);
		ts.tv_nsec = static_cast<long>((ticks % 10000000ULL) * 100ULL);
		return ts;
	};

	timespec times[2];
	times[0] = to_timespec(access, st.st_atime);
	times[1] = to_timespec(write, st.st_mtime);
	return ::futimens(fd, times) == 0 ? 1 : 0;
}

// ------------------------------------------------------------ sockets

// Winsock names the BSD socket API differently and needs explicit startup.
// The underlying calls are the same ones POSIX provides, so only the spelling
// differs here.
using SOCKET = int;

#if !defined(INVALID_SOCKET)
#define INVALID_SOCKET (-1)
#endif
#if !defined(SOCKET_ERROR)
#define SOCKET_ERROR (-1)
#endif

inline int closesocket(SOCKET s) { return ::close(s); }

// Winsock requires this before any socket call; the BSD API does not.
inline int WSAStartup(unsigned short version, void* data) { (void)version; (void)data; return 0; }
inline int WSACleanup() { return 0; }
inline int WSAGetLastError() { return errno; }

// --------------------------------------------------- shell / messages

#if !defined(SW_HIDE)
#define SW_HIDE 0
#endif
#if !defined(SW_SHOWNORMAL)
#define SW_SHOWNORMAL 1
#endif
#if !defined(SW_SHOW)
#define SW_SHOW 5
#endif

#if !defined(MB_OK)
#define MB_OK 0x00000000u
#endif
#if !defined(MB_OKCANCEL)
#define MB_OKCANCEL 0x00000001u
#endif
#if !defined(MB_YESNO)
#define MB_YESNO 0x00000004u
#endif
#if !defined(MB_ICONERROR)
#define MB_ICONERROR 0x00000010u
#endif
#if !defined(MB_ICONWARNING)
#define MB_ICONWARNING 0x00000030u
#endif
#if !defined(MB_ICONINFORMATION)
#define MB_ICONINFORMATION 0x00000040u
#endif
#if !defined(IDOK)
#define IDOK 1
#endif
#if !defined(IDCANCEL)
#define IDCANCEL 2
#endif
#if !defined(IDYES)
#define IDYES 6
#endif
#if !defined(IDNO)
#define IDNO 7
#endif

namespace pob_win_compat {

// Runs a command with its arguments, without a shell. Used by the two
// shell-style helpers below, which both need to hand a path or URL to the
// desktop without it being reinterpreted by /bin/sh.
inline bool SpawnDetached(const char* program, const char* arg)
{
	const pid_t pid = ::fork();
	if (pid < 0) return false;
	if (pid == 0) {
		::setsid();
		const char* argv[3] = { program, arg, nullptr };
		::execvp(program, const_cast<char* const*>(argv));
		::_exit(127);
	}
	// Reaped by the double-fork-free path: the child is in its own session and
	// this process does not wait, so it is inherited by init when it exits.
	return true;
}

} // namespace pob_win_compat

// Win32 (Shell): opens a document or URL with whatever is registered for it.
// macOS has one command for exactly this.
inline HANDLE ShellExecuteW(HWND parent, const wchar_t* verb, const wchar_t* file,
                            const wchar_t* parameters, const wchar_t* directory, int showCmd)
{
	(void)parent; (void)verb; (void)parameters; (void)directory; (void)showCmd;
	if (file == nullptr) return nullptr;
	const std::string target = pob_win_compat::WideToUtf8(file);
#if defined(__APPLE__)
	const bool ok = pob_win_compat::SpawnDetached("open", target.c_str());
#else
	const bool ok = pob_win_compat::SpawnDetached("xdg-open", target.c_str());
#endif
	// Win32 returns an instance handle above 32 on success; callers only test
	// against that threshold.
	return ok ? reinterpret_cast<HANDLE>(static_cast<intptr_t>(42)) : nullptr;
}

inline HANDLE ShellExecuteA(HWND parent, const char* verb, const char* file,
                            const char* parameters, const char* directory, int showCmd)
{
	(void)parent; (void)verb; (void)parameters; (void)directory; (void)showCmd;
	if (file == nullptr) return nullptr;
#if defined(__APPLE__)
	const bool ok = pob_win_compat::SpawnDetached("open", file);
#else
	const bool ok = pob_win_compat::SpawnDetached("xdg-open", file);
#endif
	return ok ? reinterpret_cast<HANDLE>(static_cast<intptr_t>(42)) : nullptr;
}

// Win32: a modal message box. There is no equivalent that can be raised from
// arbitrary code here -- a native alert needs the main thread and an event
// loop, and the launcher already draws its own dialogs through ImGui. The
// message goes to stderr so it is not lost, and the answer is the default one.
inline int MessageBoxW(HWND parent, const wchar_t* text, const wchar_t* caption, unsigned int type)
{
	(void)parent;
	const std::string body = (text != nullptr) ? pob_win_compat::WideToUtf8(text) : std::string();
	const std::string head = (caption != nullptr) ? pob_win_compat::WideToUtf8(caption) : std::string();
	std::fprintf(stderr, "[%s] %s\n", head.c_str(), body.c_str());
	if ((type & MB_YESNO) == MB_YESNO) return IDNO;      // the safe answer
	if ((type & MB_OKCANCEL) == MB_OKCANCEL) return IDCANCEL;
	return IDOK;
}

inline int MessageBoxA(HWND parent, const char* text, const char* caption, unsigned int type)
{
	(void)parent;
	std::fprintf(stderr, "[%s] %s\n", caption ? caption : "", text ? text : "");
	if ((type & MB_YESNO) == MB_YESNO) return IDNO;
	if ((type & MB_OKCANCEL) == MB_OKCANCEL) return IDCANCEL;
	return IDOK;
}

// ------------------------------------------------------------- audio

// Win32 (MCI): a string-command interface to the multimedia system. Only
// "play <file>" and "close" reach it here, so the file is handed to the
// platform's own player instead of emulating the command language.
inline unsigned long mciSendStringW(const wchar_t* command, wchar_t* returnValue,
                                    unsigned int returnLength, HWND callback)
{
	(void)callback;
	if (returnValue != nullptr && returnLength > 0) returnValue[0] = L'\0';
	if (command == nullptr) return 1;
	const std::string cmd = pob_win_compat::WideToUtf8(command);
	// "play <path> ..." -- take the quoted or unquoted path that follows.
	if (cmd.compare(0, 5, "play ") == 0) {
		std::string path = cmd.substr(5);
		const size_t q1 = path.find('"');
		if (q1 != std::string::npos) {
			const size_t q2 = path.find('"', q1 + 1);
			if (q2 != std::string::npos) path = path.substr(q1 + 1, q2 - q1 - 1);
		} else {
			const size_t sp = path.find(' ');
			if (sp != std::string::npos) path = path.substr(0, sp);
		}
#if defined(__APPLE__)
		return pob_win_compat::SpawnDetached("afplay", path.c_str()) ? 0 : 1;
#else
		return pob_win_compat::SpawnDetached("aplay", path.c_str()) ? 0 : 1;
#endif
	}
	// "close" / "stop" and anything else: nothing to do, report success so the
	// caller does not treat it as a failure.
	return 0;
}

// ------------------------------------------------------ window stubs

// These reach code paths the launcher only takes on Windows (its own window is
// a GLFW window, which it addresses through GLFW). Answering "no window"
// is both true and what the callers handle.
inline HWND GetActiveWindow() { return nullptr; }
inline HWND GetForegroundWindow() { return nullptr; }
inline int PostMessageW(HWND window, unsigned int message, WPARAM w, LPARAM l)
{
	(void)window; (void)message; (void)w; (void)l;
	return 0;
}
inline int IsWindow(HWND window) { return window != nullptr ? 0 : 0; }
inline int IsIconic(HWND window) { (void)window; return 0; }
inline int IsZoomed(HWND window) { (void)window; return 0; }
inline int IsWindowVisible(HWND window) { (void)window; return 0; }

// ----------------------------------------------------- misc typedefs

using INT_PTR = intptr_t;
using UINT_PTR = uintptr_t;
using DWORD_PTR = uintptr_t;
using SSIZE_T = intptr_t;
using SIZE_T = size_t;

struct WSADATA {
	unsigned short wVersion;
	unsigned short wHighVersion;
	char szDescription[257];
	char szSystemStatus[129];
};

#if !defined(MAKEWORD)
#define MAKEWORD(a, b) ((unsigned short)(((unsigned char)(a)) | ((unsigned short)((unsigned char)(b))) << 8))
#endif

inline int _wcsnicmp(const wchar_t* a, const wchar_t* b, size_t count)
{
	for (size_t i = 0; i < count; i++) {
		if (a[i] == 0 && b[i] == 0) return 0;
		wchar_t ca = a[i], cb = b[i];
		if (ca >= L'A' && ca <= L'Z') ca = static_cast<wchar_t>(ca + 32);
		if (cb >= L'A' && cb <= L'Z') cb = static_cast<wchar_t>(cb + 32);
		if (ca != cb) return ca < cb ? -1 : 1;
	}
	return 0;
}

// ------------------------------------------------- process management

#if !defined(HANDLE_FLAG_INHERIT)
#define HANDLE_FLAG_INHERIT 0x00000001u
#endif
#if !defined(STARTF_USESTDHANDLES)
#define STARTF_USESTDHANDLES 0x00000100u
#endif
#if !defined(STARTF_USESHOWWINDOW)
#define STARTF_USESHOWWINDOW 0x00000001u
#endif
#if !defined(CREATE_NO_WINDOW)
#define CREATE_NO_WINDOW 0x08000000u
#endif
#if !defined(CREATE_NEW_CONSOLE)
#define CREATE_NEW_CONSOLE 0x00000010u
#endif
#if !defined(DETACHED_PROCESS)
#define DETACHED_PROCESS 0x00000008u
#endif
#if !defined(STILL_ACTIVE)
#define STILL_ACTIVE 259u
#endif
#if !defined(INFINITE)
#define INFINITE 0xFFFFFFFFu
#endif

struct STARTUPINFOW {
	uint32_t cb;
	wchar_t* lpReserved;
	wchar_t* lpDesktop;
	wchar_t* lpTitle;
	unsigned long dwX, dwY, dwXSize, dwYSize;
	unsigned long dwXCountChars, dwYCountChars, dwFillAttribute;
	uint32_t dwFlags;
	unsigned short wShowWindow;
	unsigned short cbReserved2;
	unsigned char* lpReserved2;
	HANDLE hStdInput;
	HANDLE hStdOutput;
	HANDLE hStdError;
};
using STARTUPINFO = STARTUPINFOW;

struct PROCESS_INFORMATION {
	HANDLE hProcess;
	HANDLE hThread;
	uint32_t dwProcessId;
	uint32_t dwThreadId;
};

namespace pob_win_compat {

// A process handle, told apart from file and event handles by its own magic.
struct ProcessState {
	static constexpr unsigned int kMagic = 0x50524F31u; // "PRO1"
	unsigned int magic = kMagic;
	pid_t pid = -1;
	bool reaped = false;
	int status = 0;
};

inline ProcessState* AsProcess(HANDLE h)
{
	const uintptr_t v = reinterpret_cast<uintptr_t>(h);
	if (h == nullptr || h == INVALID_HANDLE_VALUE || v < 0x10000) return nullptr;
	auto* candidate = reinterpret_cast<ProcessState*>(h);
	return candidate->magic == ProcessState::kMagic ? candidate : nullptr;
}

// Splits a Win32 command line the way CommandLineToArgvW does for the shapes
// the launcher produces: a quoted program path followed by plain arguments.
inline std::vector<std::string> SplitCommandLine(const std::string& line)
{
	std::vector<std::string> out;
	std::string current;
	bool inQuotes = false, has = false;
	for (size_t i = 0; i < line.size(); i++) {
		const char c = line[i];
		if (c == '"') { inQuotes = !inQuotes; has = true; continue; }
		if (!inQuotes && (c == ' ' || c == '\t')) {
			if (has) { out.push_back(current); current.clear(); has = false; }
			continue;
		}
		current.push_back(c);
		has = true;
	}
	if (has) out.push_back(current);
	return out;
}

} // namespace pob_win_compat

// Win32: starts a process. Only the parts the launcher uses are honoured --
// the command line, the working directory and inherited std handles. The
// creation flags that concern consoles and windows have no counterpart and are
// ignored rather than approximated.
inline int CreateProcessW(const wchar_t* applicationName, wchar_t* commandLine,
                          void* processAttributes, void* threadAttributes,
                          int inheritHandles, unsigned long creationFlags,
                          void* environment, const wchar_t* currentDirectory,
                          STARTUPINFOW* startupInfo, PROCESS_INFORMATION* out)
{
	(void)processAttributes; (void)threadAttributes; (void)inheritHandles;
	(void)creationFlags; (void)environment;
	if (out == nullptr) return 0;

	std::vector<std::string> argv;
	if (applicationName != nullptr) {
		argv.push_back(pob_win_compat::WideToUtf8(applicationName));
		if (commandLine != nullptr) {
			auto rest = pob_win_compat::SplitCommandLine(pob_win_compat::WideToUtf8(commandLine));
			// The command line repeats the program as argv[0] on Windows.
			if (!rest.empty()) rest.erase(rest.begin());
			for (const std::string& a : rest) argv.push_back(a);
		}
	} else if (commandLine != nullptr) {
		argv = pob_win_compat::SplitCommandLine(pob_win_compat::WideToUtf8(commandLine));
	}
	if (argv.empty()) return 0;

	const std::string dir = (currentDirectory != nullptr)
		? pob_win_compat::WideToUtf8(currentDirectory) : std::string();

	int stdFds[3] = { -1, -1, -1 };
	if (startupInfo != nullptr && (startupInfo->dwFlags & STARTF_USESTDHANDLES) != 0) {
		stdFds[0] = pob_win_compat::HandleToFd(startupInfo->hStdInput);
		stdFds[1] = pob_win_compat::HandleToFd(startupInfo->hStdOutput);
		stdFds[2] = pob_win_compat::HandleToFd(startupInfo->hStdError);
	}

	const pid_t pid = ::fork();
	if (pid < 0) return 0;
	if (pid == 0) {
		if (!dir.empty()) {
			if (::chdir(dir.c_str()) != 0) ::_exit(126);
		}
		for (int i = 0; i < 3; i++) {
			if (stdFds[i] >= 0 && stdFds[i] != i) ::dup2(stdFds[i], i);
		}
		std::vector<char*> raw;
		raw.reserve(argv.size() + 1);
		for (std::string& a : argv) raw.push_back(const_cast<char*>(a.c_str()));
		raw.push_back(nullptr);
		::execvp(raw[0], raw.data());
		::_exit(127);
	}

	auto* state = new pob_win_compat::ProcessState();
	state->pid = pid;
	out->hProcess = reinterpret_cast<HANDLE>(state);
	out->hThread = nullptr;   // no separate thread handle exists here
	out->dwProcessId = static_cast<unsigned long>(pid);
	out->dwThreadId = 0;
	return 1;
}

// Win32: the exit code, or STILL_ACTIVE while the process runs.
inline int GetExitCodeProcess(HANDLE h, uint32_t* exitCode)
{
	auto* state = pob_win_compat::AsProcess(h);
	if (state == nullptr || exitCode == nullptr) return 0;
	if (!state->reaped) {
		int status = 0;
		const pid_t r = ::waitpid(state->pid, &status, WNOHANG);
		if (r == state->pid) { state->reaped = true; state->status = status; }
		else { *exitCode = STILL_ACTIVE; return 1; }
	}
	if (WIFEXITED(state->status))        *exitCode = static_cast<unsigned long>(WEXITSTATUS(state->status));
	else if (WIFSIGNALED(state->status)) *exitCode = static_cast<unsigned long>(128 + WTERMSIG(state->status));
	else                                 *exitCode = STILL_ACTIVE;
	return 1;
}

inline int TerminateProcess(HANDLE h, unsigned int exitCode)
{
	(void)exitCode;
	auto* state = pob_win_compat::AsProcess(h);
	if (state == nullptr) return 0;
	return ::kill(state->pid, SIGKILL) == 0 ? 1 : 0;
}

inline HANDLE OpenProcess(unsigned long access, int inheritHandle, unsigned long processId)
{
	(void)access; (void)inheritHandle;
	// Confirm the process exists before handing back a handle for it.
	if (::kill(static_cast<pid_t>(processId), 0) != 0 && errno != EPERM) return nullptr;
	auto* state = new pob_win_compat::ProcessState();
	state->pid = static_cast<pid_t>(processId);
	return reinterpret_cast<HANDLE>(state);
}

inline int QueryFullProcessImageNameW(HANDLE h, unsigned long flags, wchar_t* buffer, uint32_t* size)
{
	(void)flags;
	auto* state = pob_win_compat::AsProcess(h);
	if (state == nullptr || buffer == nullptr || size == nullptr) return 0;
	// Resolving another process's executable path needs either libproc (not
	// available through this SDK's public headers) or elevated rights. The one
	// caller uses it to label a diagnostic line, so reporting "unknown" costs
	// nothing and keeps the shim free of a private dependency.
	(void)state;
	buffer[0] = L'\0';
	*size = 0;
	return 0;
}

inline int CreatePipe(HANDLE* readEnd, HANDLE* writeEnd, void* attributes, unsigned long size)
{
	(void)attributes; (void)size;
	if (readEnd == nullptr || writeEnd == nullptr) return 0;
	int fds[2] = { -1, -1 };
	if (::pipe(fds) != 0) return 0;
	*readEnd = pob_win_compat::FdToHandle(fds[0]);
	*writeEnd = pob_win_compat::FdToHandle(fds[1]);
	return 1;
}

// Win32: controls whether a handle is inherited by child processes. The POSIX
// equivalent is the close-on-exec flag, with the sense inverted.
inline int SetHandleInformation(HANDLE h, unsigned long mask, unsigned long flags)
{
	const int fd = pob_win_compat::HandleToFd(h);
	if (fd < 0 || (mask & HANDLE_FLAG_INHERIT) == 0) return 0;
	int current = ::fcntl(fd, F_GETFD);
	if (current < 0) return 0;
	if ((flags & HANDLE_FLAG_INHERIT) != 0) current &= ~FD_CLOEXEC;
	else                                    current |= FD_CLOEXEC;
	return ::fcntl(fd, F_SETFD, current) == 0 ? 1 : 0;
}

// --------------------------------------------------------- registry

// Win32 keeps per-user settings in the registry. Nothing equivalent exists
// here, and the launcher's own settings live in its .ini file, so these report
// "not found" -- which is the branch the callers already handle for a fresh
// install that has never written the key.

using HKEY = void*;

#if !defined(HKEY_CURRENT_USER)
#define HKEY_CURRENT_USER (reinterpret_cast<HKEY>(static_cast<intptr_t>(0x80000001)))
#endif
#if !defined(HKEY_LOCAL_MACHINE)
#define HKEY_LOCAL_MACHINE (reinterpret_cast<HKEY>(static_cast<intptr_t>(0x80000002)))
#endif
#if !defined(KEY_READ)
#define KEY_READ 0x20019u
#endif
#if !defined(KEY_WRITE)
#define KEY_WRITE 0x20006u
#endif
#if !defined(REG_SZ)
#define REG_SZ 1u
#endif
#if !defined(REG_DWORD)
#define REG_DWORD 4u
#endif
#if !defined(RRF_RT_REG_SZ)
#define RRF_RT_REG_SZ 0x00000002u
#endif
#if !defined(RRF_RT_REG_DWORD)
#define RRF_RT_REG_DWORD 0x00000010u
#endif

inline long RegOpenKeyExW(HKEY key, const wchar_t* subKey, unsigned long options,
                          unsigned long desired, HKEY* result)
{
	(void)key; (void)subKey; (void)options; (void)desired;
	if (result != nullptr) *result = nullptr;
	return static_cast<long>(ERROR_FILE_NOT_FOUND);
}

inline long RegCreateKeyExW(HKEY key, const wchar_t* subKey, unsigned long reserved,
                            wchar_t* keyClass, unsigned long options, unsigned long desired,
                            void* security, HKEY* result, uint32_t* disposition)
{
	(void)key; (void)subKey; (void)reserved; (void)keyClass; (void)options;
	(void)desired; (void)security; (void)disposition;
	if (result != nullptr) *result = nullptr;
	return static_cast<long>(ERROR_ACCESS_DENIED);
}

inline long RegQueryValueExW(HKEY key, const wchar_t* name, uint32_t* reserved,
                             uint32_t* type, unsigned char* data, uint32_t* size)
{
	(void)key; (void)name; (void)reserved; (void)type; (void)data; (void)size;
	return static_cast<long>(ERROR_FILE_NOT_FOUND);
}

inline long RegSetValueExW(HKEY key, const wchar_t* name, unsigned long reserved,
                           unsigned long type, const unsigned char* data, unsigned long size)
{
	(void)key; (void)name; (void)reserved; (void)type; (void)data; (void)size;
	return static_cast<long>(ERROR_ACCESS_DENIED);
}

inline long RegGetValueW(HKEY key, const wchar_t* subKey, const wchar_t* value,
                         unsigned long flags, uint32_t* type, void* data, uint32_t* size)
{
	(void)key; (void)subKey; (void)value; (void)flags; (void)type; (void)data; (void)size;
	return static_cast<long>(ERROR_FILE_NOT_FOUND);
}

inline long RegDeleteKeyW(HKEY key, const wchar_t* subKey)
{
	(void)key; (void)subKey;
	return static_cast<long>(ERROR_FILE_NOT_FOUND);
}

inline long RegDeleteTreeW(HKEY key, const wchar_t* subKey)
{
	(void)key; (void)subKey;
	return static_cast<long>(ERROR_FILE_NOT_FOUND);
}

inline long RegCloseKey(HKEY key) { (void)key; return static_cast<long>(ERROR_SUCCESS); }

// ------------------------------------------------- named mutexes

// Win32 uses a named mutex for "is another copy already running". The POSIX
// equivalent with the same lifetime guarantee is an advisory lock on a file in
// the temp directory: it is released when the process exits, however it exits.

#if !defined(SYNCHRONIZE)
#define SYNCHRONIZE 0x00100000u
#endif
#if !defined(MUTEX_ALL_ACCESS)
#define MUTEX_ALL_ACCESS 0x001F0001u
#endif

namespace pob_win_compat {

struct MutexState {
	static constexpr unsigned int kMagic = 0x4D555431u; // "MUT1"
	unsigned int magic = kMagic;
	int fd = -1;
	std::string path;
	bool held = false;
};

inline MutexState* AsMutex(HANDLE h)
{
	const uintptr_t v = reinterpret_cast<uintptr_t>(h);
	if (h == nullptr || h == INVALID_HANDLE_VALUE || v < 0x10000) return nullptr;
	auto* candidate = reinterpret_cast<MutexState*>(h);
	return candidate->magic == MutexState::kMagic ? candidate : nullptr;
}

inline std::string MutexPathFor(const std::string& name)
{
	const char* tmp = std::getenv("TMPDIR");
	std::string dir = (tmp != nullptr && tmp[0] != 0) ? tmp : "/tmp";
	if (dir.back() != '/') dir.push_back('/');
	std::string safe;
	for (char c : name) {
		safe.push_back((c == '/' || c == '\\' || c == ':') ? '_' : c);
	}
	return dir + "pobtools-" + safe + ".lock";
}

inline bool CloseMutexHandle(HANDLE h)
{
	auto* state = AsMutex(h);
	if (state == nullptr) return false;
	if (state->fd >= 0) {
		if (state->held) ::flock(state->fd, LOCK_UN);
		::close(state->fd);
	}
	delete state;
	return true;
}

} // namespace pob_win_compat

inline HANDLE CreateMutexW(void* security, int initialOwner, const wchar_t* name)
{
	(void)security;
	const std::string label = (name != nullptr)
		? pob_win_compat::WideToUtf8(name) : std::string("unnamed");
	auto* state = new pob_win_compat::MutexState();
	state->path = pob_win_compat::MutexPathFor(label);
	state->fd = ::open(state->path.c_str(), O_CREAT | O_RDWR, 0644);
	if (state->fd < 0) { delete state; return nullptr; }

	// A Win32 named mutex exists for exactly as long as some handle to it
	// does, and OpenMutex succeeds while it exists. A lock file cannot say
	// that by existing -- it outlives every process that ever touched it --
	// so HOLDING A LOCK on it is what stands for "a handle is open". Shared,
	// because any number of handles may be open at once; exclusive when the
	// caller asked to own it.
	state->held = ::flock(state->fd, initialOwner ? (LOCK_EX | LOCK_NB)
	                                              : (LOCK_SH | LOCK_NB)) == 0;
	if (initialOwner && !state->held) {
		// Someone else owns it: Win32 still returns a handle and reports the
		// already-exists condition through GetLastError.
		errno = EEXIST;
	}
	return reinterpret_cast<HANDLE>(state);
}

inline HANDLE OpenMutexW(unsigned long access, int inheritHandle, const wchar_t* name)
{
	(void)access; (void)inheritHandle;
	if (name == nullptr) return nullptr;
	const std::string path = pob_win_compat::MutexPathFor(pob_win_compat::WideToUtf8(name));
	const int fd = ::open(path.c_str(), O_RDWR);
	if (fd < 0) return nullptr;   // never created: no such object

	// The file outlives the processes that used it, so its mere existence
	// proves nothing -- a stale lock file from a previous run would report
	// every named mutex as held forever. Taking the lock is the real test: if
	// it is free, nobody holds a handle, and the Win32 answer is "no such
	// object".
	if (::flock(fd, LOCK_EX | LOCK_NB) == 0) {
		::flock(fd, LOCK_UN);
		::close(fd);
		errno = ENOENT;
		return nullptr;
	}
	auto* state = new pob_win_compat::MutexState();
	state->path = path;
	state->fd = fd;
	return reinterpret_cast<HANDLE>(state);
}

inline int ReleaseMutex(HANDLE h)
{
	auto* state = pob_win_compat::AsMutex(h);
	if (state == nullptr || state->fd < 0) return 0;
	if (state->held) {
		::flock(state->fd, LOCK_UN);
		state->held = false;
	}
	return 1;
}

// ------------------------------------------------- known folders

// Win32 (Shell) identifies well-known directories by GUID. Only Documents is
// asked for here, so the identifier is reduced to a tag and resolved against
// the home directory.
enum PobKnownFolder { PobKnownFolderDocuments = 1 };

#if !defined(FOLDERID_Documents)
#define FOLDERID_Documents PobKnownFolderDocuments
#endif
#if !defined(FOLDERID_LocalAppData)
#define FOLDERID_LocalAppData 2
#endif
#if !defined(FOLDERID_RoamingAppData)
#define FOLDERID_RoamingAppData 3
#endif

inline long SHGetKnownFolderPath(int folder, unsigned long flags, HANDLE token, wchar_t** out)
{
	(void)flags; (void)token;
	if (out == nullptr) return -1;
	*out = nullptr;
	const char* home = std::getenv("HOME");
	if (home == nullptr || home[0] == 0) return -1;

	std::string path(home);
	if (folder == PobKnownFolderDocuments) {
		path += "/Documents";
	} else {
		// The two app-data folders map to where macOS keeps per-app state.
		path += "/Library/Application Support";
	}
	// Win32 hands back memory the caller frees with CoTaskMemFree, which is
	// mapped to free() below.
	wchar_t* buffer = static_cast<wchar_t*>(std::malloc((path.size() + 1) * sizeof(wchar_t)));
	if (buffer == nullptr) return -1;
	pob_win_compat::Utf8ToWide(path.c_str(), buffer, path.size() + 1);
	*out = buffer;
	return 0; // S_OK
}

inline void CoTaskMemFree(void* p) { std::free(p); }
inline long CoInitializeEx(void* reserved, unsigned long model) { (void)reserved; (void)model; return 0; }
inline void CoUninitialize() {}

using HINSTANCE = void*;

inline int _wputenv_s(const wchar_t* name, const wchar_t* value)
{
	if (name == nullptr) return EINVAL;
	const std::string n = pob_win_compat::WideToUtf8(name);
	if (value == nullptr || value[0] == L'\0') {
		return ::unsetenv(n.c_str()) == 0 ? 0 : errno;
	}
	const std::string v = pob_win_compat::WideToUtf8(value);
	return ::setenv(n.c_str(), v.c_str(), 1) == 0 ? 0 : errno;
}

// -------------------------------------------- window class / HRESULT

#if !defined(SUCCEEDED)
#define SUCCEEDED(hr) (((long)(hr)) >= 0)
#endif
#if !defined(FAILED)
#define FAILED(hr) (((long)(hr)) < 0)
#endif
#if !defined(S_OK)
#define S_OK 0L
#endif
#if !defined(E_FAIL)
#define E_FAIL ((long)0x80004005L)
#endif
#if !defined(REG_EXPAND_SZ)
#define REG_EXPAND_SZ 2u
#endif
#if !defined(REG_BINARY)
#define REG_BINARY 3u
#endif

using HRESULT = long;
using ATOM = unsigned short;

// Win32 window classes exist for the WebView2 host window, which is not built
// on this platform. The calls are reachable from shared code paths, so they
// answer rather than being absent.
inline int DestroyWindow(HWND window) { (void)window; return 0; }
inline int UnregisterClassW(const wchar_t* className, HINSTANCE instance)
{
	(void)className; (void)instance;
	return 0;
}
inline int CloseWindow(HWND window) { (void)window; return 0; }
inline int SetWindowTextW(HWND window, const wchar_t* text) { (void)window; (void)text; return 0; }
inline int GetWindowTextW(HWND window, wchar_t* text, int count)
{
	(void)window;
	if (text != nullptr && count > 0) text[0] = L'\0';
	return 0;
}
inline int GetWindowTextLengthW(HWND window) { (void)window; return 0; }
inline int SetForegroundWindow(HWND window) { (void)window; return 0; }
inline HWND GetTopWindow(HWND parent) { (void)parent; return nullptr; }
inline HWND GetWindow(HWND window, unsigned int relationship)
{
	(void)window; (void)relationship;
	return nullptr;
}
inline int GetWindowThreadProcessId(HWND window, uint32_t* processId)
{
	(void)window;
	if (processId != nullptr) *processId = 0;
	return 0;
}
inline int GetClassNameW(HWND window, wchar_t* className, int count)
{
	(void)window;
	if (className != nullptr && count > 0) className[0] = L'\0';
	return 0;
}

#if !defined(GW_OWNER)
#define GW_OWNER 4u
#endif
#if !defined(SMTO_ABORTIFHUNG)
#define SMTO_ABORTIFHUNG 0x0002u
#endif
#if !defined(SMTO_BLOCK)
#define SMTO_BLOCK 0x0001u
#endif
#if !defined(WM_NULL)
#define WM_NULL 0x0000u
#endif
#if !defined(WM_COPYDATA)
#define WM_COPYDATA 0x004Au
#endif

inline int SendMessageTimeoutW(HWND window, unsigned int message, WPARAM w, LPARAM l,
                               unsigned int flags, unsigned int timeout, DWORD_PTR* result)
{
	(void)window; (void)message; (void)w; (void)l; (void)flags; (void)timeout;
	if (result != nullptr) *result = 0;
	return 0;
}

inline LRESULT SendMessageW(HWND window, unsigned int message, WPARAM w, LPARAM l)
{
	(void)window; (void)message; (void)w; (void)l;
	return 0;
}

inline int EnumWindows(int (*callback)(HWND, LPARAM), LPARAM param)
{
	(void)callback; (void)param;
	// No window list to walk: this process's own window is a GLFW window and
	// the launcher reaches it through GLFW.
	return 1;
}

// ------------------------------------------------- file dialogs

// Win32 opens the system file chooser through this structure. macOS has one
// too, but reaching it needs the main thread and an AppKit run loop, which a
// call from arbitrary code cannot assume. osascript asks the desktop for the
// same native panel from a child process, which works from anywhere.

#if !defined(OFN_FILEMUSTEXIST)
#define OFN_FILEMUSTEXIST 0x00001000u
#endif
#if !defined(OFN_PATHMUSTEXIST)
#define OFN_PATHMUSTEXIST 0x00000800u
#endif
#if !defined(OFN_OVERWRITEPROMPT)
#define OFN_OVERWRITEPROMPT 0x00000002u
#endif
#if !defined(OFN_NOCHANGEDIR)
#define OFN_NOCHANGEDIR 0x00000008u
#endif
#if !defined(OFN_EXPLORER)
#define OFN_EXPLORER 0x00080000u
#endif
#if !defined(OFN_HIDEREADONLY)
#define OFN_HIDEREADONLY 0x00000004u
#endif

struct OPENFILENAMEW {
	uint32_t lStructSize;
	HWND hwndOwner;
	HINSTANCE hInstance;
	const wchar_t* lpstrFilter;
	wchar_t* lpstrCustomFilter;
	uint32_t nMaxCustFilter;
	uint32_t nFilterIndex;
	wchar_t* lpstrFile;
	uint32_t nMaxFile;
	wchar_t* lpstrFileTitle;
	uint32_t nMaxFileTitle;
	const wchar_t* lpstrInitialDir;
	const wchar_t* lpstrTitle;
	uint32_t Flags;
	unsigned short nFileOffset;
	unsigned short nFileExtension;
	const wchar_t* lpstrDefExt;
	LPARAM lCustData;
	void* lpfnHook;
	const wchar_t* lpTemplateName;
	void* pvReserved;
	uint32_t dwReserved;
	uint32_t FlagsEx;
};
using OPENFILENAME = OPENFILENAMEW;

namespace pob_win_compat {

// Runs a command and returns its standard output, trimmed. Used for the file
// panels below.
inline std::string CaptureCommand(const std::vector<std::string>& argv)
{
	int fds[2] = { -1, -1 };
	if (::pipe(fds) != 0) return std::string();
	const pid_t pid = ::fork();
	if (pid < 0) { ::close(fds[0]); ::close(fds[1]); return std::string(); }
	if (pid == 0) {
		::close(fds[0]);
		::dup2(fds[1], STDOUT_FILENO);
		::close(fds[1]);
		std::vector<char*> raw;
		for (const std::string& a : argv) raw.push_back(const_cast<char*>(a.c_str()));
		raw.push_back(nullptr);
		::execvp(raw[0], raw.data());
		::_exit(127);
	}
	::close(fds[1]);
	std::string out;
	char buf[1024];
	for (;;) {
		const ssize_t n = ::read(fds[0], buf, sizeof(buf));
		if (n < 0) { if (errno == EINTR) continue; break; }
		if (n == 0) break;
		out.append(buf, static_cast<size_t>(n));
	}
	::close(fds[0]);
	int status = 0;
	::waitpid(pid, &status, 0);
	while (!out.empty() && (out.back() == '\n' || out.back() == '\r')) out.pop_back();
	if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) return std::string();
	return out;
}

inline bool RunFilePanel(OPENFILENAMEW* ofn, bool saving)
{
	if (ofn == nullptr || ofn->lpstrFile == nullptr || ofn->nMaxFile == 0) return false;
	std::string prompt = "Choose a file";
	if (ofn->lpstrTitle != nullptr) prompt = WideToUtf8(ofn->lpstrTitle);
	// Quotes inside the prompt would end the AppleScript string.
	std::string safe;
	for (char c : prompt) {
		if (c != '"' && c != '\\') safe.push_back(c);
	}
	const std::string script = saving
		? ("POSIX path of (choose file name with prompt \"" + safe + "\")")
		: ("POSIX path of (choose file with prompt \"" + safe + "\")");
	const std::string path = CaptureCommand({ "osascript", "-e", script });
	if (path.empty()) return false;   // cancelled, or no desktop to ask
	Utf8ToWide(path.c_str(), ofn->lpstrFile, ofn->nMaxFile);
	return true;
}

} // namespace pob_win_compat

inline int GetOpenFileNameW(OPENFILENAMEW* ofn)
{
	return pob_win_compat::RunFilePanel(ofn, false) ? 1 : 0;
}

inline int GetSaveFileNameW(OPENFILENAMEW* ofn)
{
	return pob_win_compat::RunFilePanel(ofn, true) ? 1 : 0;
}

// ------------------------------------------------------ odds and ends

#if !defined(WM_CLOSE)
#define WM_CLOSE 0x0010u
#endif
#if !defined(WM_QUIT)
#define WM_QUIT 0x0012u
#endif
#if !defined(WM_CHAR)
#define WM_CHAR 0x0102u
#endif

struct OSVERSIONINFOW {
	uint32_t dwOSVersionInfoSize;
	uint32_t dwMajorVersion;
	uint32_t dwMinorVersion;
	uint32_t dwBuildNumber;
	uint32_t dwPlatformId;
	wchar_t szCSDVersion[128];
};

// Win32 window properties, used by the WebView2 host window only.
inline HANDLE GetPropW(HWND window, const wchar_t* name)
{
	(void)window; (void)name;
	return nullptr;
}
inline int SetPropW(HWND window, const wchar_t* name, HANDLE value)
{
	(void)window; (void)name; (void)value;
	return 0;
}
inline HANDLE RemovePropW(HWND window, const wchar_t* name)
{
	(void)window; (void)name;
	return nullptr;
}

// Win32 clipboard. The launcher's own clipboard access goes through GLFW,
// which works on every platform; these are the raw calls a Windows-only path
// uses, and reporting "could not open" sends the caller down the branch it
// already has for a clipboard another process is holding.
inline int OpenClipboard(HWND owner) { (void)owner; return 0; }
inline int CloseClipboard() { return 0; }
inline int EmptyClipboard() { return 0; }
inline HANDLE GetClipboardData(unsigned int format) { (void)format; return nullptr; }
inline HANDLE SetClipboardData(unsigned int format, HANDLE data)
{
	(void)format; (void)data;
	return nullptr;
}
inline void* GlobalLock(HANDLE h) { return h; }
inline int GlobalUnlock(HANDLE h) { (void)h; return 0; }
inline HANDLE GlobalAlloc(unsigned int flags, size_t size)
{
	(void)flags;
	return std::malloc(size);
}
inline HANDLE GlobalFree(HANDLE h) { std::free(h); return nullptr; }

#if !defined(CF_UNICODETEXT)
#define CF_UNICODETEXT 13u
#endif
#if !defined(GMEM_MOVEABLE)
#define GMEM_MOVEABLE 0x0002u
#endif

// --------------------------------------------- remaining constants

#if !defined(FILE_BEGIN)
#define FILE_BEGIN 0u
#endif
#if !defined(FILE_CURRENT)
#define FILE_CURRENT 1u
#endif
#if !defined(FILE_END)
#define FILE_END 2u
#endif
#if !defined(FILE_WRITE_ATTRIBUTES)
#define FILE_WRITE_ATTRIBUTES 0x00000100u
#endif
#if !defined(FILE_FLAG_SEQUENTIAL_SCAN)
#define FILE_FLAG_SEQUENTIAL_SCAN 0x08000000u
#endif
#if !defined(FILE_ATTRIBUTE_READONLY)
#define FILE_ATTRIBUTE_READONLY 0x00000001u
#endif
#if !defined(ERROR_FILE_EXISTS)
#define ERROR_FILE_EXISTS 80u
#endif
#if !defined(WAIT_ABANDONED)
#define WAIT_ABANDONED 128u
#endif
#if !defined(STD_INPUT_HANDLE)
#define STD_INPUT_HANDLE 0xFFFFFFF6u
#endif
#if !defined(STD_OUTPUT_HANDLE)
#define STD_OUTPUT_HANDLE 0xFFFFFFF5u
#endif
#if !defined(STD_ERROR_HANDLE)
#define STD_ERROR_HANDLE 0xFFFFFFF4u
#endif
#if !defined(KEY_QUERY_VALUE)
#define KEY_QUERY_VALUE 0x0001u
#endif
#if !defined(KEY_SET_VALUE)
#define KEY_SET_VALUE 0x0002u
#endif
#if !defined(REG_OPTION_NON_VOLATILE)
#define REG_OPTION_NON_VOLATILE 0u
#endif
#if !defined(KF_FLAG_DEFAULT)
#define KF_FLAG_DEFAULT 0u
#endif
#if !defined(CREATE_UNICODE_ENVIRONMENT)
#define CREATE_UNICODE_ENVIRONMENT 0x00000400u
#endif
#if !defined(DONT_RESOLVE_DLL_REFERENCES)
#define DONT_RESOLVE_DLL_REFERENCES 0x00000001u
#endif
#if !defined(LOAD_WITH_ALTERED_SEARCH_PATH)
#define LOAD_WITH_ALTERED_SEARCH_PATH 0x00000008u
#endif
#if !defined(PROCESS_QUERY_LIMITED_INFORMATION)
#define PROCESS_QUERY_LIMITED_INFORMATION 0x1000u
#endif
#if !defined(SD_BOTH)
#define SD_BOTH 2
#endif
#if !defined(_TRUNCATE)
#define _TRUNCATE ((size_t)-1)
#endif
#if !defined(PM_REMOVE)
#define PM_REMOVE 0x0001u
#endif
#if !defined(SW_RESTORE)
#define SW_RESTORE 9
#endif
#if !defined(SW_SHOWNA)
#define SW_SHOWNA 8
#endif
#if !defined(GW_HWNDNEXT)
#define GW_HWNDNEXT 2u
#endif
#if !defined(GUI_INMOVESIZE)
#define GUI_INMOVESIZE 0x00000002u
#endif
#if !defined(BI_RGB)
#define BI_RGB 0u
#endif
#if !defined(SND_ASYNC)
#define SND_ASYNC 0x0001u
#endif
#if !defined(SND_NODEFAULT)
#define SND_NODEFAULT 0x0002u
#endif
#if !defined(SND_ALIAS)
#define SND_ALIAS 0x00010000u
#endif
#if !defined(SHCNE_ASSOCCHANGED)
#define SHCNE_ASSOCCHANGED 0x08000000u
#endif
#if !defined(SHCNF_IDLIST)
#define SHCNF_IDLIST 0u
#endif
#if !defined(BIF_RETURNONLYFSDIRS)
#define BIF_RETURNONLYFSDIRS 0x0001u
#endif
#if !defined(BIF_NEWDIALOGSTYLE)
#define BIF_NEWDIALOGSTYLE 0x0040u
#endif
#if !defined(BFFM_INITIALIZED)
#define BFFM_INITIALIZED 1u
#endif
#if !defined(BFFM_SETSELECTIONW)
#define BFFM_SETSELECTIONW 0x0467u
#endif
#if !defined(EXCEPTION_ACCESS_VIOLATION)
#define EXCEPTION_ACCESS_VIOLATION 0xC0000005u
#endif
#if !defined(JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE)
#define JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE 0x00002000u
#endif
#if !defined(JobObjectExtendedLimitInformation)
#define JobObjectExtendedLimitInformation 9
#endif
#if !defined(WINHTTP_DISABLE_COOKIES)
#define WINHTTP_DISABLE_COOKIES 0x00000001u
#endif
#if !defined(WINHTTP_OPTION_DISABLE_FEATURE)
#define WINHTTP_OPTION_DISABLE_FEATURE 63u
#endif

// Window styles: the WebView2 host window is the only user, and it is not
// built here, but the constants appear in shared declarations.
#if !defined(WS_OVERLAPPEDWINDOW)
#define WS_OVERLAPPEDWINDOW 0x00CF0000u
#endif
#if !defined(WS_POPUP)
#define WS_POPUP 0x80000000u
#endif
#if !defined(WS_VISIBLE)
#define WS_VISIBLE 0x10000000u
#endif
#if !defined(WS_CAPTION)
#define WS_CAPTION 0x00C00000u
#endif
#if !defined(WS_SYSMENU)
#define WS_SYSMENU 0x00080000u
#endif
#if !defined(WS_THICKFRAME)
#define WS_THICKFRAME 0x00040000u
#endif
#if !defined(WS_MINIMIZEBOX)
#define WS_MINIMIZEBOX 0x00020000u
#endif
#if !defined(WS_MAXIMIZEBOX)
#define WS_MAXIMIZEBOX 0x00010000u
#endif
#if !defined(WS_MAXIMIZE)
#define WS_MAXIMIZE 0x01000000u
#endif

using PVOID = void*;
using LPBYTE = unsigned char*;
using HGLOBAL = void*;
using LPITEMIDLIST = void*;

struct SECURITY_ATTRIBUTES {
	uint32_t nLength;
	void* lpSecurityDescriptor;
	int bInheritHandle;
};

struct COPYDATASTRUCT {
	ULONG_PTR dwData;
	uint32_t cbData;
	void* lpData;
};

struct MSG {
	HWND hwnd;
	unsigned int message;
	WPARAM wParam;
	LPARAM lParam;
	uint32_t time;
	struct { long x, y; } pt;
};

struct WNDCLASSW {
	unsigned int style;
	WNDPROC lpfnWndProc;
	int cbClsExtra;
	int cbWndExtra;
	HINSTANCE hInstance;
	HANDLE hIcon;
	HANDLE hCursor;
	HANDLE hbrBackground;
	const wchar_t* lpszMenuName;
	const wchar_t* lpszClassName;
};

struct WNDCLASSEXW {
	unsigned int cbSize;
	unsigned int style;
	WNDPROC lpfnWndProc;
	int cbClsExtra;
	int cbWndExtra;
	HINSTANCE hInstance;
	HANDLE hIcon;
	HANDLE hCursor;
	HANDLE hbrBackground;
	const wchar_t* lpszMenuName;
	const wchar_t* lpszClassName;
	HANDLE hIconSm;
};

struct GUITHREADINFO {
	uint32_t cbSize;
	uint32_t flags;
	HWND hwndActive;
	HWND hwndFocus;
	HWND hwndCapture;
	HWND hwndMenuOwner;
	HWND hwndMoveSize;
	HWND hwndCaret;
	struct { long left, top, right, bottom; } rcCaret;
};

struct BROWSEINFOW {
	HWND hwndOwner;
	LPITEMIDLIST pidlRoot;
	wchar_t* pszDisplayName;
	const wchar_t* lpszTitle;
	unsigned int ulFlags;
	int (*lpfn)(HWND, unsigned int, LPARAM, LPARAM);
	LPARAM lParam;
	int iImage;
};

#pragma pack(push, 1)
struct BITMAPFILEHEADER {
	unsigned short bfType;
	uint32_t bfSize;
	unsigned short bfReserved1;
	unsigned short bfReserved2;
	uint32_t bfOffBits;
};
#pragma pack(pop)

struct BITMAPINFOHEADER {
	uint32_t biSize;
	long biWidth;
	long biHeight;
	unsigned short biPlanes;
	unsigned short biBitCount;
	uint32_t biCompression;
	uint32_t biSizeImage;
	long biXPelsPerMeter;
	long biYPelsPerMeter;
	uint32_t biClrUsed;
	uint32_t biClrImportant;
};

struct JOBOBJECT_EXTENDED_LIMIT_INFORMATION {
	struct {
		long long PerProcessUserTimeLimit;
		long long PerJobUserTimeLimit;
		uint32_t LimitFlags;
		size_t MinimumWorkingSetSize;
		size_t MaximumWorkingSetSize;
		uint32_t ActiveProcessLimit;
		uintptr_t Affinity;
		uint32_t PriorityClass;
		uint32_t SchedulingClass;
	} BasicLimitInformation;
	struct { unsigned long long ReadOperationCount, WriteOperationCount, OtherOperationCount,
	                            ReadTransferCount, WriteTransferCount, OtherTransferCount; } IoInfo;
	size_t ProcessMemoryLimit;
	size_t JobMemoryLimit;
	size_t PeakProcessMemoryUsed;
	size_t PeakJobMemoryUsed;
};

// --------------------------------------------- remaining functions

inline void SecureZeroMemory(void* p, size_t size)
{
	// Win32 guarantees the write is not optimised away; a volatile cursor does
	// the same here.
	volatile unsigned char* cursor = static_cast<volatile unsigned char*>(p);
	for (size_t i = 0; i < size; i++) cursor[i] = 0;
}

inline HANDLE GetStdHandle(unsigned long which)
{
	if (which == STD_INPUT_HANDLE)  return pob_win_compat::FdToHandle(STDIN_FILENO);
	if (which == STD_OUTPUT_HANDLE) return pob_win_compat::FdToHandle(STDOUT_FILENO);
	if (which == STD_ERROR_HANDLE)  return pob_win_compat::FdToHandle(STDERR_FILENO);
	return INVALID_HANDLE_VALUE;
}

inline int GetDiskFreeSpaceExW(const wchar_t* path, ULARGE_INTEGER* freeForCaller,
                               ULARGE_INTEGER* total, ULARGE_INTEGER* totalFree)
{
	const std::string utf8 = (path != nullptr) ? pob_win_compat::WideToPath(path) : std::string("/");
	struct statvfs st = {};
	if (::statvfs(utf8.c_str(), &st) != 0) return 0;
	const unsigned long long blockSize = (st.f_frsize != 0) ? st.f_frsize : st.f_bsize;
	if (freeForCaller != nullptr) freeForCaller->QuadPart = blockSize * st.f_bavail;
	if (total != nullptr)         total->QuadPart = blockSize * st.f_blocks;
	if (totalFree != nullptr)     totalFree->QuadPart = blockSize * st.f_bfree;
	return 1;
}

inline int CompareFileTime(const FILETIME* a, const FILETIME* b)
{
	auto value = [](const FILETIME* t) -> unsigned long long {
		if (t == nullptr) return 0;
		return (static_cast<unsigned long long>(t->dwHighDateTime) << 32) | t->dwLowDateTime;
	};
	const unsigned long long x = value(a), y = value(b);
	return (x < y) ? -1 : (x > y) ? 1 : 0;
}

// Win32 stores times in UTC and converts for display. Both forms are the same
// instant here, so the conversion is a copy; the display side already uses
// FileTimeToSystemTime, which applies the local offset.
inline int FileTimeToLocalFileTime(const FILETIME* in, FILETIME* out)
{
	if (in == nullptr || out == nullptr) return 0;
	*out = *in;
	return 1;
}

inline unsigned long ExpandEnvironmentStringsW(const wchar_t* input, wchar_t* output, unsigned long size)
{
	if (input == nullptr) return 0;
	const std::string in = pob_win_compat::WideToUtf8(input);
	std::string out;
	for (size_t i = 0; i < in.size(); i++) {
		if (in[i] != '%') { out.push_back(in[i]); continue; }
		const size_t end = in.find('%', i + 1);
		if (end == std::string::npos) { out.push_back(in[i]); continue; }
		const std::string name = in.substr(i + 1, end - i - 1);
		const char* value = std::getenv(name.c_str());
		if (value != nullptr) out += value;
		i = end;
	}
	if (output == nullptr || out.size() + 1 > size) {
		return static_cast<unsigned long>(out.size() + 1);
	}
	return static_cast<unsigned long>(pob_win_compat::Utf8ToWide(out.c_str(), output, size)) + 1;
}

inline wchar_t* GetEnvironmentStringsW()
{
	// Win32 returns one block of "NAME=VALUE\0...\0\0". Rebuilt here from
	// environ so the caller's parsing works unchanged.
	extern char** environ;
	std::string block;
	for (char** e = environ; e != nullptr && *e != nullptr; ++e) {
		block.append(*e);
		block.push_back('\0');
	}
	block.push_back('\0');
	wchar_t* out = static_cast<wchar_t*>(std::malloc((block.size() + 1) * sizeof(wchar_t)));
	if (out == nullptr) return nullptr;
	for (size_t i = 0; i < block.size(); i++) {
		out[i] = static_cast<wchar_t>(static_cast<unsigned char>(block[i]));
	}
	out[block.size()] = L'\0';
	return out;
}

inline int FreeEnvironmentStringsW(wchar_t* block) { std::free(block); return 1; }

inline unsigned long GetCommandLineLengthUnused() { return 0; }

namespace pob_win_compat {

// The launcher's entry point re-derives argv from the command line, so main()
// stores it here first (see host/host_main_posix.cpp).
inline std::wstring& SavedCommandLine()
{
	static std::wstring line;
	return line;
}

inline void SaveCommandLine(int argc, char** argv)
{
	std::string joined;
	for (int i = 0; i < argc; i++) {
		if (i != 0) joined.push_back(' ');
		// Quote anything with a space so the splitter puts it back together.
		const bool needsQuotes = std::strchr(argv[i], ' ') != nullptr;
		if (needsQuotes) joined.push_back('"');
		joined += argv[i];
		if (needsQuotes) joined.push_back('"');
	}
	std::wstring wide(joined.size() + 1, L'\0');
	Utf8ToWide(joined.c_str(), &wide[0], wide.size());
	wide.resize(std::wcslen(wide.c_str()));
	SavedCommandLine() = wide;
}

} // namespace pob_win_compat

inline const wchar_t* GetCommandLineW()
{
	return pob_win_compat::SavedCommandLine().c_str();
}

inline unsigned long GetSystemDirectoryW(wchar_t* buffer, unsigned int size)
{
	// There is no system directory to point at; the caller falls back when
	// this reports zero.
	if (buffer != nullptr && size > 0) buffer[0] = L'\0';
	return 0;
}

inline unsigned long GetShortPathNameW(const wchar_t* longPath, wchar_t* shortPath, unsigned long size)
{
	// 8.3 short paths do not exist here, and the long path is always valid
	// where a short one would be used.
	if (longPath == nullptr) return 0;
	const std::string utf8 = pob_win_compat::WideToPath(longPath);
	if (shortPath == nullptr || utf8.size() + 1 > size) {
		return static_cast<unsigned long>(utf8.size() + 1);
	}
	return static_cast<unsigned long>(pob_win_compat::Utf8ToWide(utf8.c_str(), shortPath, size));
}

inline int AllowSetForegroundWindow(unsigned long processId) { (void)processId; return 0; }
inline LRESULT DefWindowProcW(HWND w, unsigned int m, WPARAM wp, LPARAM lp)
{
	(void)w; (void)m; (void)wp; (void)lp;
	return 0;
}
inline int GetGUIThreadInfo(unsigned long threadId, GUITHREADINFO* info)
{
	(void)threadId; (void)info;
	return 0;
}
inline int PlaySoundW(const wchar_t* sound, HINSTANCE instance, unsigned long flags)
{
	(void)instance; (void)flags;
	if (sound == nullptr) return 0;
	const std::string path = pob_win_compat::WideToUtf8(sound);
#if defined(__APPLE__)
	return pob_win_compat::SpawnDetached("afplay", path.c_str()) ? 1 : 0;
#else
	return pob_win_compat::SpawnDetached("aplay", path.c_str()) ? 1 : 0;
#endif
}

// Job objects group processes so that closing the group kills them. The POSIX
// counterpart is the process group, which the launcher does not set up here;
// reporting failure leaves the children it spawns unmanaged, which is the same
// outcome as on a Windows where the job could not be created.
inline HANDLE CreateJobObjectW(void* attributes, const wchar_t* name)
{
	(void)attributes; (void)name;
	return nullptr;
}
inline int AssignProcessToJobObject(HANDLE job, HANDLE process)
{
	(void)job; (void)process;
	return 0;
}
inline int SetInformationJobObject(HANDLE job, int infoClass, void* info, unsigned long length)
{
	(void)job; (void)infoClass; (void)info; (void)length;
	return 0;
}

// MSVC's secure time functions: argument order is (out, in), the inverse of
// the POSIX reentrant forms.
inline int localtime_s(struct tm* out, const time_t* when)
{
	if (out == nullptr || when == nullptr) return EINVAL;
	return ::localtime_r(when, out) != nullptr ? 0 : EINVAL;
}

inline int gmtime_s(struct tm* out, const time_t* when)
{
	if (out == nullptr || when == nullptr) return EINVAL;
	return ::gmtime_r(when, out) != nullptr ? 0 : EINVAL;
}

inline int _wfreopen_s(FILE** out, const wchar_t* path, const wchar_t* mode, FILE* stream)
{
	if (out == nullptr) return EINVAL;
	const std::string p = pob_win_compat::WideToPath(path);
	const std::string m = pob_win_compat::WideToUtf8(mode);
	*out = std::freopen(p.c_str(), m.c_str(), stream);
	return *out != nullptr ? 0 : (errno != 0 ? errno : EINVAL);
}

// ------------------------------------------- remaining odds and ends

#if !defined(FILE_READ_ATTRIBUTES)
#define FILE_READ_ATTRIBUTES 0x00000080u
#endif
#if !defined(FILE_SHARE_DELETE)
#define FILE_SHARE_DELETE 0x00000004u
#endif
#if !defined(FILE_FLAG_BACKUP_SEMANTICS)
#define FILE_FLAG_BACKUP_SEMANTICS 0x02000000u
#endif
#if !defined(FILE_FLAG_OPEN_REPARSE_POINT)
#define FILE_FLAG_OPEN_REPARSE_POINT 0x00200000u
#endif
#if !defined(FSCTL_GET_REPARSE_POINT)
#define FSCTL_GET_REPARSE_POINT 0x000900A8u
#endif
#if !defined(FSCTL_SET_REPARSE_POINT)
#define FSCTL_SET_REPARSE_POINT 0x000900A4u
#endif

struct EXCEPTION_RECORD {
	uint32_t ExceptionCode;
	uint32_t ExceptionFlags;
	EXCEPTION_RECORD* ExceptionRecord;
	void* ExceptionAddress;
	uint32_t NumberParameters;
	uintptr_t ExceptionInformation[15];
};

// The engine's header forward-declares _EXCEPTION_POINTERS; this is the
// definition, for the one file that reads its fields.
struct _EXCEPTION_POINTERS {
	EXCEPTION_RECORD* ExceptionRecord;
	CONTEXT* ContextRecord;
};
using EXCEPTION_POINTERS = _EXCEPTION_POINTERS;

// Win32 reaches a reparse point's target through a device control code. The
// POSIX equivalent is readlink, which the one caller's shape maps onto.
inline int DeviceIoControl(HANDLE h, unsigned long code, void* input, unsigned long inputSize,
                           void* output, unsigned long outputSize,
                           uint32_t* returned, void* overlapped)
{
	(void)h; (void)code; (void)input; (void)inputSize;
	(void)output; (void)outputSize; (void)overlapped;
	if (returned != nullptr) *returned = 0;
	// Reporting failure sends the caller down its "not a reparse point" path,
	// which is correct for everything this shim's CreateFileW can open.
	return 0;
}

inline int CancelSynchronousIo(HANDLE thread)
{
	(void)thread;
	// Nothing to cancel: the POSIX reader this would interrupt uses poll with
	// a timeout instead (see engine/headless_ipc_posix.cpp).
	return 0;
}

inline double _wtof(const wchar_t* text)
{
	if (text == nullptr) return 0.0;
	const std::string utf8 = pob_win_compat::WideToUtf8(text);
	return std::atof(utf8.c_str());
}

// Win32 (MSVC): sscanf for wide strings. The _s form also takes buffer sizes
// for %s conversions, which the call sites here do not use.
#define swscanf_s swscanf

// ----------------------------------------- message loop / shell stubs

inline int ShowWindow(HWND window, int command) { (void)window; (void)command; return 0; }
inline HWND CreateWindowExW(unsigned long exStyle, const wchar_t* className,
                            const wchar_t* windowName, unsigned long style,
                            int x, int y, int width, int height,
                            HWND parent, HMENU menu, HINSTANCE instance, void* param)
{
	(void)exStyle; (void)className; (void)windowName; (void)style;
	(void)x; (void)y; (void)width; (void)height;
	(void)parent; (void)menu; (void)instance; (void)param;
	return nullptr;
}
inline ATOM RegisterClassW(const WNDCLASSW* cls) { (void)cls; return 0; }
inline ATOM RegisterClassExW(const WNDCLASSEXW* cls) { (void)cls; return 0; }
inline int PeekMessageW(MSG* message, HWND window, unsigned int first,
                        unsigned int last, unsigned int remove)
{
	(void)message; (void)window; (void)first; (void)last; (void)remove;
	return 0;
}
inline int GetMessageW(MSG* message, HWND window, unsigned int first, unsigned int last)
{
	(void)message; (void)window; (void)first; (void)last;
	return 0;
}
inline int TranslateMessage(const MSG* message) { (void)message; return 0; }
inline LRESULT DispatchMessageW(const MSG* message) { (void)message; return 0; }
inline void PostQuitMessage(int code) { (void)code; }
inline unsigned int SetTimer(HWND window, uintptr_t id, unsigned int elapse, void* callback)
{
	(void)window; (void)id; (void)elapse; (void)callback;
	return 0;
}
inline int KillTimer(HWND window, uintptr_t id) { (void)window; (void)id; return 0; }
inline HANDLE LoadCursorW(HINSTANCE instance, const wchar_t* name)
{
	(void)instance; (void)name;
	return nullptr;
}
inline HANDLE LoadIconW(HINSTANCE instance, const wchar_t* name)
{
	(void)instance; (void)name;
	return nullptr;
}
inline HANDLE CreateSolidBrush(unsigned long colour) { (void)colour; return nullptr; }
inline HDC GetDC(HWND window) { (void)window; return nullptr; }
inline int ReleaseDC(HWND window, HDC dc) { (void)window; (void)dc; return 0; }
inline int GetDeviceCaps(HDC dc, int index) { (void)dc; (void)index; return 0; }
inline int GetClientRect(HWND window, void* rect) { (void)window; (void)rect; return 0; }
inline int GetWindowRect(HWND window, void* rect) { (void)window; (void)rect; return 0; }

// Win32 (Shell): the folder picker. osascript asks the desktop for the native
// panel, the same way the file pickers above do.
inline LPITEMIDLIST SHBrowseForFolderW(BROWSEINFOW* info)
{
	if (info == nullptr) return nullptr;
	std::string prompt = "Choose a folder";
	if (info->lpszTitle != nullptr) prompt = pob_win_compat::WideToUtf8(info->lpszTitle);
	std::string safe;
	for (char c : prompt) {
		if (c != '"' && c != '\\') safe.push_back(c);
	}
	const std::string path = pob_win_compat::CaptureCommand(
		{ "osascript", "-e", "POSIX path of (choose folder with prompt \"" + safe + "\")" });
	if (path.empty()) return nullptr;
	// The caller turns this into a path with SHGetPathFromIDListW, so the
	// "id list" is just the chosen path, kept alive for that call.
	char* copy = static_cast<char*>(std::malloc(path.size() + 1));
	if (copy == nullptr) return nullptr;
	std::memcpy(copy, path.c_str(), path.size() + 1);
	return copy;
}

inline int SHGetPathFromIDListW(LPITEMIDLIST list, wchar_t* path)
{
	if (list == nullptr || path == nullptr) return 0;
	const char* chosen = static_cast<const char*>(list);
	pob_win_compat::Utf8ToWide(chosen, path, MAX_PATH);
	return 1;
}

inline void SHChangeNotify(long event, unsigned int flags, const void* a, const void* b)
{
	(void)event; (void)flags; (void)a; (void)b;
	// Win32 uses this to tell the shell that file associations changed. There
	// is nothing to notify here: the launcher does not register any.
}

// Win32: splits a command line into argv. The POSIX entry point already
// receives argv, but one caller re-derives it from the command line, which is
// empty here (see GetCommandLineW), so this returns an empty vector with the
// count set to zero rather than inventing arguments.
inline wchar_t** CommandLineToArgvW(const wchar_t* commandLine, int* count)
{
	if (count == nullptr) return nullptr;
	const std::vector<std::string> parts = (commandLine != nullptr)
		? pob_win_compat::SplitCommandLine(pob_win_compat::WideToUtf8(commandLine))
		: std::vector<std::string>();
	*count = static_cast<int>(parts.size());
	// One allocation holding the pointer array followed by the strings, so a
	// single LocalFree releases everything, as on Windows.
	size_t bytes = sizeof(wchar_t*) * (parts.size() + 1);
	for (const std::string& part : parts) bytes += (part.size() + 1) * sizeof(wchar_t);
	auto* block = static_cast<unsigned char*>(std::malloc(bytes ? bytes : sizeof(wchar_t*)));
	if (block == nullptr) { *count = 0; return nullptr; }
	auto** argv = reinterpret_cast<wchar_t**>(block);
	auto* text = reinterpret_cast<wchar_t*>(block + sizeof(wchar_t*) * (parts.size() + 1));
	for (size_t i = 0; i < parts.size(); i++) {
		argv[i] = text;
		const size_t written = pob_win_compat::Utf8ToWide(parts[i].c_str(), text, parts[i].size() + 1);
		text += written + 1;
	}
	argv[parts.size()] = nullptr;
	return argv;
}

// Win32 (MSVC): the share modes of _fsopen, from <share.h>. POSIX has no
// mandatory sharing, so the mode is accepted and ignored -- a reader is never
// blocked here, which is what the one caller wants anyway.
#if !defined(_SH_DENYNO)
#define _SH_DENYNO 0x40
#endif
#if !defined(_SH_DENYWR)
#define _SH_DENYWR 0x20
#endif
#if !defined(_SH_DENYRD)
#define _SH_DENYRD 0x30
#endif
#if !defined(_SH_DENYRW)
#define _SH_DENYRW 0x10
#endif

inline FILE* _fsopen(const char* path, const char* mode, int shareFlag)
{
	(void)shareFlag;
	return std::fopen(path, mode);
}

inline FILE* _wfsopen(const wchar_t* path, const wchar_t* mode, int shareFlag)
{
	(void)shareFlag;
	const std::string p = pob_win_compat::WideToPath(path);
	const std::string m = pob_win_compat::WideToUtf8(mode);
	return std::fopen(p.c_str(), m.c_str());
}

// ------------------------------------------------------- format helpers

namespace pob_win_compat {

// MSVC's wide formatters read %s as a WIDE string and %S as a narrow one. The
// C standard -- and so this platform's vswprintf -- reads them the other way
// round. The mismatch is not a compile error: it reads the wrong kind of
// pointer at run time, and the conversion simply produces nothing. That is why
// the diagnostic log, whose file name is built with "%s-<date>.log", was never
// written.
//
// Rewriting the specifiers here rather than at the call sites keeps the
// callers as Windows wants them, and covers every one of them at once.
inline std::wstring PosixWideFormat(const wchar_t* format)
{
	std::wstring out;
	if (format == nullptr) return out;
	for (const wchar_t* p = format; *p != L'\0'; ++p) {
		out.push_back(*p);
		if (*p != L'%') continue;
		++p;
		if (*p == L'\0') break;
		if (*p == L'%') { out.push_back(*p); continue; }

		std::wstring mods;
		while (*p != L'\0' && std::wcschr(L"-+ #0", *p) != nullptr) mods.push_back(*p++);
		while (*p >= L'0' && *p <= L'9') mods.push_back(*p++);
		if (*p == L'.') {
			mods.push_back(*p++);
			while (*p >= L'0' && *p <= L'9') mods.push_back(*p++);
		}
		std::wstring len;
		while (*p != L'\0' && std::wcschr(L"hlLjztqw", *p) != nullptr) len.push_back(*p++);
		if (*p == L'\0') { out += mods; out += len; break; }

		wchar_t conv = *p;
		if (conv == L's' || conv == L'c') {
			// Wide unless the caller said otherwise; 'w' is MSVC's spelling of
			// the same thing, 'h' its spelling of the narrow form.
			if (len == L"w" || len.empty()) len = L"l";
			else if (len == L"h") len.clear();
		} else if (conv == L'S' || conv == L'C') {
			conv = (conv == L'S') ? L's' : L'c';
			len.clear();
		}
		out += mods;
		out += len;
		out.push_back(conv);
	}
	return out;
}

} // namespace pob_win_compat

// Win32 (MSVC): the bounds-checked wide formatter. The portable swprintf takes
// the same arguments with the size in the second position, which is what the
// array-size overload below forwards.
template <size_t N>
inline int sprintf_s(char (&buffer)[N], const char* format, ...)
{
	va_list args;
	va_start(args, format);
	const int n = ::vsnprintf(buffer, N, format, args);
	va_end(args);
	return n;
}

template <size_t N>
inline int swprintf_s(wchar_t (&buffer)[N], const wchar_t* format, ...)
{
	const std::wstring fmt = pob_win_compat::PosixWideFormat(format);
	va_list args;
	va_start(args, format);
	const int n = ::vswprintf(buffer, N, fmt.c_str(), args);
	va_end(args);
	return n;
}

// The explicit-size form, which callers use for a heap buffer.
inline int swprintf_s(wchar_t* buffer, size_t size, const wchar_t* format, ...)
{
	const std::wstring fmt = pob_win_compat::PosixWideFormat(format);
	va_list args;
	va_start(args, format);
	const int n = ::vswprintf(buffer, size, fmt.c_str(), args);
	va_end(args);
	return n;
}

template <size_t N>
inline int wcscpy_s(wchar_t (&dest)[N], const wchar_t* src)
{
	if (src == nullptr) return EINVAL;
	size_t i = 0;
	for (; src[i] != L'\0' && i + 1 < N; i++) dest[i] = src[i];
	dest[i] = L'\0';
	return src[i] == L'\0' ? 0 : ERANGE;
}

template <size_t N>
inline int strcpy_s(char (&dest)[N], const char* src)
{
	if (src == nullptr) return EINVAL;
	size_t i = 0;
	for (; src[i] != '\0' && i + 1 < N; i++) dest[i] = src[i];
	dest[i] = '\0';
	return src[i] == '\0' ? 0 : ERANGE;
}

template <size_t N>
inline int wcsncpy_s(wchar_t (&dest)[N], const wchar_t* src, size_t count)
{
	if (src == nullptr) return EINVAL;
	const size_t limit = (count == static_cast<size_t>(-1)) ? N - 1 : count;
	size_t i = 0;
	for (; src[i] != L'\0' && i < limit && i + 1 < N; i++) dest[i] = src[i];
	dest[i] = L'\0';
	return 0;
}

#endif // !_WIN32
