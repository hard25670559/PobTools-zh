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
#include <ctime>
#include <dirent.h>
#include <fcntl.h>
#include <fnmatch.h>
#include <string>
#include <sys/resource.h>
#include <sys/stat.h>
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

	const std::string utf8 = pob_win_compat::WideToUtf8(path);
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
                     unsigned long* written, void* overlapped)
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
                    unsigned long* read, void* overlapped)
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

inline int CloseHandle(HANDLE h)
{
	// Events and files share the handle type, as they do on Windows.
	if (auto* state = pob_win_compat::AsEvent(h)) {
		delete state;
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
	const std::string utf8 = pob_win_compat::WideToUtf8(path);
	return ::mkdir(utf8.c_str(), 0755) == 0 ? 1 : 0;
}

inline int CreateDirectoryA(const char* path, void* security)
{
	(void)security;
	if (path == nullptr) return 0;
	return ::mkdir(path, 0755) == 0 ? 1 : 0;
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

// Win32: the last error of the calling thread. The engine compares it against
// one value only (a sharing violation, to pick a per-process log name), which
// POSIX never produces, so errno is reported as-is and that branch is simply
// never taken.
inline unsigned long GetLastError()
{
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
struct FILETIME {
	unsigned long dwLowDateTime;
	unsigned long dwHighDateTime;
};

union ULARGE_INTEGER {
	struct { unsigned long LowPart; unsigned long HighPart; };
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

	if (output == nullptr || outputSize == 0) {
		// An upper bound: never more wide characters than input bytes, plus
		// the terminator when the input was given as terminated.
		return static_cast<int>(slice.size()) + (inputLen < 0 ? 1 : 0);
	}
	const size_t written = pob_win_compat::Utf8ToWide(slice.c_str(), output,
	                                                  static_cast<size_t>(outputSize));
	return static_cast<int>(inputLen < 0 ? written + 1 : written);
}

// ------------------------------------------- performance counter / misc

// Win32: a signed 64-bit value split in two halves.
union LARGE_INTEGER {
	struct { unsigned long LowPart; long HighPart; };
	long long QuadPart;
};

// Win32: the attribute bits of a wide path. Same contract as the narrow form.
inline unsigned long GetFileAttributesW(const wchar_t* path)
{
	if (path == nullptr) return INVALID_FILE_ATTRIBUTES;
	const std::string utf8 = pob_win_compat::WideToUtf8(path);
	return GetFileAttributesA(utf8.c_str());
}

inline int SetFileAttributesW(const wchar_t* path, unsigned long attributes)
{
	// Win32 attributes have no POSIX counterpart worth emulating -- the one
	// use here is clearing the read-only bit, which chmod covers.
	if (path == nullptr) return 0;
	const std::string utf8 = pob_win_compat::WideToUtf8(path);
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
	const std::string utf8 = pob_win_compat::WideToUtf8(path);
	return ::unlink(utf8.c_str()) == 0 ? 1 : 0;
}

inline int DeleteFileA(const char* path)
{
	if (path == nullptr) return 0;
	return ::unlink(path) == 0 ? 1 : 0;
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
	unsigned long dwFileAttributes;
	wchar_t cFileName[MAX_PATH];
};

namespace pob_win_compat {

// Backing state for a FindFirstFileW walk: the open directory plus the
// filename glob split off the caller's path.
struct FindState {
	DIR* dir = nullptr;
	std::string pattern;
};

inline bool FillFindData(const std::string& dirPath, const dirent* entry, WIN32_FIND_DATAW* out)
{
	std::string full = dirPath;
	if (!full.empty() && full.back() != '/') full.push_back('/');
	full += entry->d_name;

	struct stat st = {};
	out->dwFileAttributes = FILE_ATTRIBUTE_NORMAL;
	if (::stat(full.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) {
		out->dwFileAttributes = FILE_ATTRIBUTE_DIRECTORY;
	}
	return Utf8ToWide(entry->d_name, out->cFileName, MAX_PATH) > 0;
}

} // namespace pob_win_compat

// Win32: begins a directory walk matching a path that ends in a glob.
inline HANDLE FindFirstFileW(const wchar_t* pathWithGlob, WIN32_FIND_DATAW* out)
{
	if (pathWithGlob == nullptr || out == nullptr) return INVALID_HANDLE_VALUE;
	const std::string full = pob_win_compat::WideToUtf8(pathWithGlob);
	const size_t slash = full.find_last_of("/\\");
	const std::string dirPath = (slash == std::string::npos) ? std::string(".") : full.substr(0, slash);
	const std::string pattern = (slash == std::string::npos) ? full : full.substr(slash + 1);

	DIR* dir = ::opendir(dirPath.empty() ? "." : dirPath.c_str());
	if (dir == nullptr) return INVALID_HANDLE_VALUE;

	auto* state = new pob_win_compat::FindState{ dir, pattern };
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
	// The directory path is recoverable from the descriptor, but the entries
	// only need their own names from here on.
	for (dirent* entry = ::readdir(state->dir); entry != nullptr; entry = ::readdir(state->dir)) {
		if (::fnmatch(state->pattern.c_str(), entry->d_name, 0) != 0) continue;
		struct stat st = {};
		out->dwFileAttributes = FILE_ATTRIBUTE_NORMAL;
		if (::fstatat(::dirfd(state->dir), entry->d_name, &st, 0) == 0 && S_ISDIR(st.st_mode)) {
			out->dwFileAttributes = FILE_ATTRIBUTE_DIRECTORY;
		}
		if (pob_win_compat::Utf8ToWide(entry->d_name, out->cFileName, MAX_PATH) > 0) return 1;
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
	const std::string utf8 = pob_win_compat::WideToUtf8(path);
	return ::rmdir(utf8.c_str()) == 0 ? 1 : 0;
}

inline int RemoveDirectoryA(const char* path)
{
	if (path == nullptr) return 0;
	return ::rmdir(path) == 0 ? 1 : 0;
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
	const std::string a = pob_win_compat::WideToUtf8(from);
	const std::string b = pob_win_compat::WideToUtf8(to);
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
	const std::string a = pob_win_compat::WideToUtf8(from);
	const std::string b = pob_win_compat::WideToUtf8(to);
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
	const std::string utf8 = pob_win_compat::WideToUtf8(path);
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
	unsigned long dwFileAttributes;
	FILETIME ftCreationTime;
	FILETIME ftLastAccessTime;
	FILETIME ftLastWriteTime;
	unsigned long nFileSizeHigh;
	unsigned long nFileSizeLow;
};

inline int GetFileAttributesExW(const wchar_t* path, int level, void* out)
{
	(void)level;
	if (path == nullptr || out == nullptr) return 0;
	const std::string utf8 = pob_win_compat::WideToUtf8(path);
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
	const std::string utf8 = pob_win_compat::WideToUtf8(path);
	char resolved[4096] = {};
	// realpath requires the path to exist; fall back to the input otherwise,
	// which is what the callers here do with a path they are about to create.
	const char* full = ::realpath(utf8.c_str(), resolved) ? resolved : utf8.c_str();
	return static_cast<unsigned long>(pob_win_compat::Utf8ToWide(full, buffer, size));
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
	*out = std::freopen(path, mode, stream);
	return *out != nullptr ? 0 : (errno != 0 ? errno : EINVAL);
}

inline int _wfopen_s(FILE** out, const wchar_t* path, const wchar_t* mode)
{
	if (out == nullptr) return EINVAL;
	const std::string p = pob_win_compat::WideToUtf8(path);
	const std::string m = pob_win_compat::WideToUtf8(mode);
	*out = std::fopen(p.c_str(), m.c_str());
	return *out != nullptr ? 0 : (errno != 0 ? errno : EINVAL);
}

inline FILE* _wfopen(const wchar_t* path, const wchar_t* mode)
{
	const std::string p = pob_win_compat::WideToUtf8(path);
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

// ------------------------------------------------------- format helpers

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
	va_list args;
	va_start(args, format);
	const int n = ::vswprintf(buffer, N, format, args);
	va_end(args);
	return n;
}

#endif // !_WIN32
