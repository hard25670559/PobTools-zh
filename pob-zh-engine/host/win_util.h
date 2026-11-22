// Shared tiny Win32 helpers (UTF-8/UTF-16 conversion, whole-file read/write, existence checks, exe dir) for .cpp files only.
#pragma once

#if defined(_WIN32)
#include <windows.h>
// rpcndr.h, which windows.h pulls in, defines `small` as `char`. Nothing in
// this program wants that, and anything with a member or variable of that name
// -- PobUi::WidgetFonts::small, for one -- stops compiling in every file that
// reaches windows.h before it. This header is included from the top of some of
// those files, so it takes the definition back out.
#undef small
#else
#include "../engine/common/win_compat.h"
#endif
#include <filesystem>
#include <string>

// The separator paths are normalised to before they are compared, stored or
// shown. Windows accepts both and writes '\\'; everywhere else '/' is the only
// one the system understands, and the only one a path should be shown as.
#if defined(_WIN32)
constexpr wchar_t kPathSep = L'\\';
constexpr wchar_t kPathSepOther = L'/';
#else
constexpr wchar_t kPathSep = L'/';
constexpr wchar_t kPathSepOther = L'\\';
#endif

// Rewrites every separator to the platform's own.
inline std::wstring native_seps(std::wstring p)
{
	for (wchar_t& c : p)
		if (c == kPathSepOther) c = kPathSep;
	return p;
}

// A std::filesystem::path built from a path string this codebase produced.
//
// Those strings are Windows-shaped: they may carry '\\' separators. On POSIX
// std::filesystem does not treat '\\' as a separator at all, so such a string
// becomes ONE file name with backslashes inside it -- the file is created, but
// under a name nothing else can find. Every std::fstream in this codebase has
// to go through here rather than construct a path directly.
inline std::filesystem::path fs_path(const std::wstring& p)
{
#if defined(_WIN32)
	return std::filesystem::path(p);
#else
	return std::filesystem::path(pob_win_compat::WideToPath(p.c_str()));
#endif
}

inline std::string narrow(const std::wstring& w)
{
	if (w.empty()) return std::string();
	int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
	std::string s(n, '\0');
	WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &s[0], n, nullptr, nullptr);
	return s;
}

inline std::wstring widen(const std::string& s)
{
	if (s.empty()) return std::wstring();
	int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
	std::wstring w(n, L'\0');
	MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], n);
	return w;
}

inline bool read_file_utf8(const std::wstring& path, std::string& out)
{
	HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
	if (h == INVALID_HANDLE_VALUE) return false;
	LARGE_INTEGER size{};
	bool ok = false;
	if (GetFileSizeEx(h, &size) && size.QuadPart >= 0 && size.QuadPart < (1ll << 30)) {
		out.resize((size_t)size.QuadPart);
		DWORD read = 0;
		ok = out.empty() || (ReadFile(h, &out[0], (DWORD)out.size(), &read, nullptr) && read == out.size());
		if (!ok) out.clear();
	}
	CloseHandle(h);
	return ok;
}

inline bool write_file_utf8(const std::wstring& path, const std::string& content)
{
	HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (h == INVALID_HANDLE_VALUE) return false;
	DWORD written = 0;
	bool ok = content.empty() ||
		(WriteFile(h, content.data(), (DWORD)content.size(), &written, nullptr) && written == content.size());
	CloseHandle(h);
	return ok;
}

inline bool file_exists(const std::wstring& p)
{
	DWORD a = GetFileAttributesW(p.c_str());
	return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

inline bool dir_exists(const std::wstring& p)
{
	DWORD a = GetFileAttributesW(p.c_str());
	return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}

inline std::wstring exe_dir()
{
	wchar_t path[MAX_PATH];
	GetModuleFileNameW(nullptr, path, MAX_PATH);
	std::wstring s(path);
	size_t slash = s.find_last_of(L"/\\");  // both: the launcher runs on either platform
	return slash == std::wstring::npos ? L"" : s.substr(0, slash + 1);
}
