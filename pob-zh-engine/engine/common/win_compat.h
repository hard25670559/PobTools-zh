// SimpleGraphic Engine
//
// Win32 compatibility shims for non-Windows targets.
//
// The engine calls a handful of Win32 functions directly. Rather than edit
// every call site (which would churn the Windows build for no reason), this
// header provides the same names on platforms that lack them. It is included
// only from the non-Windows branch of common.h, so the Windows build never
// sees it and its call sites stay byte-for-byte unchanged.

#pragma once

#if !defined(_WIN32)

#include <chrono>
#include <climits>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <pthread.h>
#include <strings.h>
#include <cerrno>
#include <cstdio>
#include <ctime>
#include <cwchar>
#include <string>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>
#if defined(__APPLE__)
#include <mach-o/dyld.h>
#endif

// Win32: the case-insensitive string compare. POSIX spells it strcasecmp.
inline int _stricmp(const char* a, const char* b)
{
	return ::strcasecmp(a, b);
}

// Win32: a loaded module. The engine uses it for one diagnostic -- asking
// whether the lua-utf8 module is in the process -- so the dynamic loader
// answers the same question here: RTLD_NOLOAD reports an already-loaded
// library and never loads one.
using HMODULE = void*;

inline HMODULE GetModuleHandleA(const char* name)
{
	if (name == nullptr) return ::dlopen(nullptr, RTLD_LAZY | RTLD_NOLOAD);
	std::string candidate(name);
	if (candidate.find(".dylib") == std::string::npos &&
	    candidate.find(".so") == std::string::npos) {
#if defined(__APPLE__)
		candidate += ".dylib";
#else
		candidate += ".so";
#endif
	}
	return ::dlopen(candidate.c_str(), RTLD_LAZY | RTLD_NOLOAD);
}

// Win32: milliseconds since boot. The monotonic clock is the same thing with
// a different zero, which is all the engine compares against.
inline unsigned long long GetTickCount64()
{
	timespec ts = {};
	::clock_gettime(CLOCK_MONOTONIC, &ts);
	return static_cast<unsigned long long>(ts.tv_sec) * 1000ULL
	     + static_cast<unsigned long long>(ts.tv_nsec) / 1000000ULL;
}

inline unsigned long GetTickCount()
{
	return static_cast<unsigned long>(GetTickCount64() & 0xFFFFFFFFULL);
}

// Win32: an opaque thread id. pthread_t is the platform's equivalent; it is
// only ever logged or compared against the current thread here.
inline unsigned long GetCurrentThreadId()
{
	return static_cast<unsigned long>(
		reinterpret_cast<uintptr_t>(::pthread_self()) & 0xFFFFFFFFULL);
}

// Win32: the process-wide crash handler hook. There is no equivalent to
// install -- a POSIX process reports a fault through a signal, and taking that
// over would change how crashes surface -- so the hook is accepted and
// discarded, which leaves the default behaviour in place.
using LPTOP_LEVEL_EXCEPTION_FILTER = void*;

template <typename Filter>
inline LPTOP_LEVEL_EXCEPTION_FILTER SetUnhandledExceptionFilter(Filter filter)
{
	(void)filter;
	return nullptr;
}

// Win32 integer and handle typedefs the engine spells out in its own code.
// Sized to match the Win64 originals so format strings and casts still line up.
using DWORD = unsigned long;
using BOOL = int;
using BYTE = unsigned char;
using WORD = unsigned short;
using UINT = unsigned int;
using LONG = int;
using ULONG = unsigned long;
using LONGLONG = long long;
using ULONGLONG = unsigned long long;

#if !defined(TRUE)
#define TRUE 1
#endif
#if !defined(FALSE)
#define FALSE 0
#endif

// Win32: the debugger output channel. There is no debugger to attach to here,
// so the text goes to stderr, which is where a POSIX process reports anyway.
inline void OutputDebugStringA(const char* text)
{
	if (text != nullptr) {
		std::fputs(text, stderr);
	}
}

// Win32: a thread's CPU register state. It only ever appears as a pointer in
// declarations here, and nothing on this platform produces one, so an
// incomplete type is all that is needed for those declarations to parse.
struct CONTEXT;

// Win32 window and message typedefs. The engine declares variables and
// function parameters with these even in code paths that only run on Windows,
// so the names have to exist for the file to parse. Nothing here implements
// window messaging: the calls that would need it are behind platform branches.
using HWND = void*;
using HIMC = void*;
using HMENU = void*;
using HDC = void*;
using WPARAM = uintptr_t;
using LPARAM = intptr_t;
using LRESULT = intptr_t;
using ULONG_PTR = uintptr_t;
using LONG_PTR = intptr_t;
using WNDPROC = void*;

// Win32: the base of the application-defined message range.
#if !defined(WM_APP)
#define WM_APP 0x8000u
#endif

// Win32 (MSVC): the bounds-checked fopen. It reports the error through its
// return value and the handle through an out-parameter, which is the shape the
// call sites expect.
inline int fopen_s(FILE** out, const char* path, const char* mode)
{
	if (out == nullptr) return EINVAL;
	*out = std::fopen(path, mode);
	if (*out == nullptr) return errno != 0 ? errno : EINVAL;
	return 0;
}

// Win32: suspends the calling thread for the given number of milliseconds.
inline void Sleep(unsigned long milliseconds)
{
	std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
}

// Win32: copies the value of an environment variable into the caller's buffer.
// Returns the number of characters written (excluding the terminator), or the
// required buffer size (including the terminator) when the buffer is too
// small, or 0 when the variable does not exist.
inline unsigned long GetEnvironmentVariableA(const char* name, char* buffer, unsigned long size)
{
	const char* value = std::getenv(name);
	if (value == nullptr) {
		if (buffer != nullptr && size > 0) {
			buffer[0] = '\0';
		}
		return 0;
	}
	const unsigned long length = static_cast<unsigned long>(std::strlen(value));
	if (buffer == nullptr || length + 1 > size) {
		return length + 1;
	}
	std::memcpy(buffer, value, length + 1);
	return length;
}

// Win32: terminates the process immediately, without running static
// destructors or atexit handlers. std::_Exit has the same contract.
[[noreturn]] inline void ExitProcess(unsigned int exitCode)
{
	std::_Exit(static_cast<int>(exitCode));
}

// Win32: the wide-character form of the above. The engine only reads ASCII
// flag values through it, but the conversion is done properly all the same.
inline unsigned long GetEnvironmentVariableW(const wchar_t* name, wchar_t* buffer, unsigned long size)
{
	// The name is a wide literal in the caller; narrow it for getenv.
	std::string narrowName;
	for (const wchar_t* p = name; *p != L'\0'; ++p) {
		narrowName.push_back(static_cast<char>(*p & 0x7F));
	}
	const char* value = std::getenv(narrowName.c_str());
	if (value == nullptr) {
		if (buffer != nullptr && size > 0) {
			buffer[0] = L'\0';
		}
		return 0;
	}
	const size_t needed = std::mbstowcs(nullptr, value, 0);
	if (needed == static_cast<size_t>(-1)) {
		if (buffer != nullptr && size > 0) {
			buffer[0] = L'\0';
		}
		return 0;
	}
	if (buffer == nullptr || needed + 1 > size) {
		return static_cast<unsigned long>(needed + 1);
	}
	std::mbstowcs(buffer, value, size);
	return static_cast<unsigned long>(needed);
}

// Win32: the longest path the platform accepts. PATH_MAX is the POSIX
// equivalent; the engine uses it to size stack buffers.
#if !defined(MAX_PATH)
#define MAX_PATH PATH_MAX
#endif

// Win32: GetFileAttributes returns this when the path does not exist.
#if !defined(INVALID_FILE_ATTRIBUTES)
#define INVALID_FILE_ATTRIBUTES 0xFFFFFFFFu
#endif
#if !defined(FILE_ATTRIBUTE_NORMAL)
#define FILE_ATTRIBUTE_NORMAL 0x00000080u
#endif
#if !defined(FILE_ATTRIBUTE_DIRECTORY)
#define FILE_ATTRIBUTE_DIRECTORY 0x00000010u
#endif

// Win32: the attribute bits of a path, or INVALID_FILE_ATTRIBUTES when it does
// not exist. The engine only ever asks "does this exist" and "is it a
// directory", which is all that is reported here.
inline unsigned long GetFileAttributesA(const char* path)
{
	struct stat st = {};
	if (path == nullptr || ::stat(path, &st) != 0) {
		return INVALID_FILE_ATTRIBUTES;
	}
	return S_ISDIR(st.st_mode) ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL;
}

// Win32: the path of the running executable when the module is null, which is
// the only form the engine uses.
inline unsigned long GetModuleFileNameA(void* module, char* buffer, unsigned long size)
{
	(void)module;
	if (buffer == nullptr || size == 0) return 0;
	buffer[0] = '\0';
#if defined(__APPLE__)
	uint32_t written = size;
	if (_NSGetExecutablePath(buffer, &written) != 0) {
		return 0; // buffer too small; Win32 would truncate, we report nothing
	}
	return static_cast<unsigned long>(std::strlen(buffer));
#else
	const ssize_t n = ::readlink("/proc/self/exe", buffer, size - 1);
	if (n <= 0) return 0;
	buffer[n] = '\0';
	return static_cast<unsigned long>(n);
#endif
}

#endif // !_WIN32
