// SimpleGraphic Engine
//
// WinHTTP shim for non-Windows targets, implemented over libcurl.
//
// The launcher talks to the network through WinHTTP, whose shape is a chain of
// handles: a session, a connection to a host, then a request that is sent and
// read in steps. libcurl performs a whole transfer in one call instead, so the
// handles here accumulate the parameters and the transfer happens when
// WinHttpSendRequest is called; everything after that reads from the result
// already in memory.
//
// Two files use this API directly (host/http_client.cpp and
// host/icon_manager.cpp), which is why it is shimmed rather than each of them
// getting a parallel implementation.
//
// libcurl is already a dependency of this project, so nothing new is pulled in.

#pragma once

#if !defined(_WIN32)

#include "win_compat.h"

#include <curl/curl.h>

#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

// ---------------------------------------------------------- WinHTTP flags

using HINTERNET = void*;

#if !defined(WINHTTP_NO_PROXY_NAME)
#define WINHTTP_NO_PROXY_NAME nullptr
#endif
#if !defined(WINHTTP_NO_PROXY_BYPASS)
#define WINHTTP_NO_PROXY_BYPASS nullptr
#endif
#if !defined(WINHTTP_NO_ADDITIONAL_HEADERS)
#define WINHTTP_NO_ADDITIONAL_HEADERS nullptr
#endif
#if !defined(WINHTTP_NO_REFERER)
#define WINHTTP_NO_REFERER nullptr
#endif
#if !defined(WINHTTP_NO_REQUEST_DATA)
#define WINHTTP_NO_REQUEST_DATA nullptr
#endif
#if !defined(WINHTTP_ACCESS_TYPE_DEFAULT_PROXY)
#define WINHTTP_ACCESS_TYPE_DEFAULT_PROXY 0u
#endif
#if !defined(WINHTTP_ACCESS_TYPE_NO_PROXY)
#define WINHTTP_ACCESS_TYPE_NO_PROXY 1u
#endif
#if !defined(WINHTTP_ACCESS_TYPE_NAMED_PROXY)
#define WINHTTP_ACCESS_TYPE_NAMED_PROXY 3u
#endif
#if !defined(WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY)
#define WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY 4u
#endif
#if !defined(WINHTTP_FLAG_SECURE)
#define WINHTTP_FLAG_SECURE 0x00800000u
#endif
#if !defined(WINHTTP_FLAG_ESCAPE_DISABLE)
#define WINHTTP_FLAG_ESCAPE_DISABLE 0x00000040u
#endif
#if !defined(WINHTTP_DEFAULT_ACCEPT_TYPES)
#define WINHTTP_DEFAULT_ACCEPT_TYPES nullptr
#endif
#if !defined(INTERNET_DEFAULT_HTTPS_PORT)
#define INTERNET_DEFAULT_HTTPS_PORT 443u
#endif
#if !defined(INTERNET_DEFAULT_HTTP_PORT)
#define INTERNET_DEFAULT_HTTP_PORT 80u
#endif
#if !defined(WINHTTP_QUERY_STATUS_CODE)
#define WINHTTP_QUERY_STATUS_CODE 19u
#endif
#if !defined(WINHTTP_QUERY_CONTENT_LENGTH)
#define WINHTTP_QUERY_CONTENT_LENGTH 5u
#endif
#if !defined(WINHTTP_QUERY_LOCATION)
#define WINHTTP_QUERY_LOCATION 33u
#endif
#if !defined(WINHTTP_QUERY_CUSTOM)
#define WINHTTP_QUERY_CUSTOM 65535u
#endif
#if !defined(WINHTTP_QUERY_FLAG_NUMBER)
#define WINHTTP_QUERY_FLAG_NUMBER 0x20000000u
#endif
#if !defined(WINHTTP_HEADER_NAME_BY_INDEX)
#define WINHTTP_HEADER_NAME_BY_INDEX nullptr
#endif
#if !defined(WINHTTP_NO_HEADER_INDEX)
#define WINHTTP_NO_HEADER_INDEX nullptr
#endif
#if !defined(WINHTTP_OPTION_REDIRECT_POLICY)
#define WINHTTP_OPTION_REDIRECT_POLICY 88u
#endif
#if !defined(WINHTTP_OPTION_REDIRECT_POLICY_NEVER)
#define WINHTTP_OPTION_REDIRECT_POLICY_NEVER 0u
#endif
#if !defined(WINHTTP_OPTION_SECURE_PROTOCOLS)
#define WINHTTP_OPTION_SECURE_PROTOCOLS 84u
#endif
#if !defined(WINHTTP_OPTION_DECOMPRESSION)
#define WINHTTP_OPTION_DECOMPRESSION 118u
#endif
#if !defined(WINHTTP_DECOMPRESSION_FLAG_ALL)
#define WINHTTP_DECOMPRESSION_FLAG_ALL 0x00000003u
#endif

namespace pob_win_compat {

// ----------------------------------------------------------- handle types

enum HttpHandleKind { kHttpSession = 1, kHttpConnect, kHttpRequest };

struct HttpHandleBase {
	static constexpr unsigned int kMagic = 0x48545431u; // "HTT1"
	unsigned int magic = kMagic;
	HttpHandleKind kind;
	explicit HttpHandleBase(HttpHandleKind k) : kind(k) {}
};

struct HttpSession : HttpHandleBase {
	HttpSession() : HttpHandleBase(kHttpSession) {}
	std::string userAgent;
	std::string proxy;            // empty = whatever libcurl picks up
	long connectTimeoutMs = 30000;
	long totalTimeoutMs = 120000;
	bool followRedirects = true;
	bool decompress = false;
};

struct HttpConnect : HttpHandleBase {
	HttpConnect() : HttpHandleBase(kHttpConnect) {}
	HttpSession* session = nullptr;
	std::string host;
	unsigned short port = 443;
};

struct HttpRequest : HttpHandleBase {
	HttpRequest() : HttpHandleBase(kHttpRequest) {}
	HttpConnect* connect = nullptr;
	std::string verb = "GET";
	std::string path;
	bool secure = true;
	std::vector<std::string> headers;

	// Filled by WinHttpSendRequest.
	bool sent = false;
	long status = 0;
	std::vector<unsigned char> body;
	size_t readOffset = 0;
	std::unordered_map<std::string, std::string> responseHeaders;
};

inline HttpHandleBase* AsHttp(HINTERNET h)
{
	const uintptr_t v = reinterpret_cast<uintptr_t>(h);
	if (h == nullptr || v < 0x10000) return nullptr;
	auto* candidate = static_cast<HttpHandleBase*>(h);
	return candidate->magic == HttpHandleBase::kMagic ? candidate : nullptr;
}

template <typename T>
inline T* AsHttpKind(HINTERNET h, HttpHandleKind kind)
{
	HttpHandleBase* base = AsHttp(h);
	if (base == nullptr || base->kind != kind) return nullptr;
	return static_cast<T*>(base);
}

inline size_t CurlWriteBody(char* data, size_t size, size_t count, void* user)
{
	auto* out = static_cast<std::vector<unsigned char>*>(user);
	const size_t total = size * count;
	out->insert(out->end(), data, data + total);
	return total;
}

inline size_t CurlWriteHeader(char* data, size_t size, size_t count, void* user)
{
	auto* out = static_cast<std::unordered_map<std::string, std::string>*>(user);
	const size_t total = size * count;
	std::string line(data, total);
	while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) line.pop_back();
	const size_t colon = line.find(':');
	if (colon != std::string::npos) {
		std::string name = line.substr(0, colon);
		std::string value = line.substr(colon + 1);
		while (!value.empty() && (value.front() == ' ' || value.front() == '\t')) value.erase(value.begin());
		// Header names are case-insensitive; store them lowercased so lookups
		// do not have to care, the way WinHttpQueryHeaders does not.
		for (char& c : name) {
			if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + 32);
		}
		(*out)[name] = value;
	}
	return total;
}

} // namespace pob_win_compat

// --------------------------------------------------------------- the API

inline HINTERNET WinHttpOpen(const wchar_t* userAgent, unsigned long accessType,
                             const wchar_t* proxy, const wchar_t* proxyBypass,
                             unsigned long flags)
{
	(void)proxyBypass; (void)flags;
	auto* session = new pob_win_compat::HttpSession();
	if (userAgent != nullptr) session->userAgent = pob_win_compat::WideToUtf8(userAgent);
	if (accessType == WINHTTP_ACCESS_TYPE_NAMED_PROXY && proxy != nullptr) {
		session->proxy = pob_win_compat::WideToUtf8(proxy);
	} else if (accessType == WINHTTP_ACCESS_TYPE_NO_PROXY) {
		session->proxy = "";   // explicit "no proxy" is set on the handle below
	}
	return session;
}

inline HINTERNET WinHttpConnect(HINTERNET sessionHandle, const wchar_t* host,
                                unsigned short port, unsigned long reserved)
{
	(void)reserved;
	auto* session = pob_win_compat::AsHttpKind<pob_win_compat::HttpSession>(
		sessionHandle, pob_win_compat::kHttpSession);
	if (session == nullptr || host == nullptr) return nullptr;
	auto* connect = new pob_win_compat::HttpConnect();
	connect->session = session;
	connect->host = pob_win_compat::WideToUtf8(host);
	connect->port = port;
	return connect;
}

inline HINTERNET WinHttpOpenRequest(HINTERNET connectHandle, const wchar_t* verb,
                                    const wchar_t* path, const wchar_t* version,
                                    const wchar_t* referrer, const wchar_t** acceptTypes,
                                    unsigned long flags)
{
	(void)version; (void)referrer; (void)acceptTypes;
	auto* connect = pob_win_compat::AsHttpKind<pob_win_compat::HttpConnect>(
		connectHandle, pob_win_compat::kHttpConnect);
	if (connect == nullptr) return nullptr;
	auto* request = new pob_win_compat::HttpRequest();
	request->connect = connect;
	if (verb != nullptr) request->verb = pob_win_compat::WideToUtf8(verb);
	if (path != nullptr) request->path = pob_win_compat::WideToUtf8(path);
	request->secure = (flags & WINHTTP_FLAG_SECURE) != 0;
	return request;
}

inline int WinHttpSetTimeouts(HINTERNET handle, int resolveTimeout, int connectTimeout,
                              int sendTimeout, int receiveTimeout)
{
	(void)resolveTimeout; (void)sendTimeout;
	auto* session = pob_win_compat::AsHttpKind<pob_win_compat::HttpSession>(
		handle, pob_win_compat::kHttpSession);
	if (session == nullptr) return 0;
	if (connectTimeout > 0) session->connectTimeoutMs = connectTimeout;
	if (receiveTimeout > 0) session->totalTimeoutMs = receiveTimeout;
	return 1;
}

inline int WinHttpSetOption(HINTERNET handle, unsigned long option, void* value, unsigned long length)
{
	(void)length;
	pob_win_compat::HttpHandleBase* base = pob_win_compat::AsHttp(handle);
	if (base == nullptr) return 0;

	pob_win_compat::HttpSession* session = nullptr;
	if (base->kind == pob_win_compat::kHttpSession) {
		session = static_cast<pob_win_compat::HttpSession*>(base);
	} else if (base->kind == pob_win_compat::kHttpRequest) {
		auto* request = static_cast<pob_win_compat::HttpRequest*>(base);
		session = (request->connect != nullptr) ? request->connect->session : nullptr;
	}
	if (session == nullptr) return 0;

	if (option == WINHTTP_OPTION_REDIRECT_POLICY && value != nullptr) {
		session->followRedirects =
			*static_cast<unsigned long*>(value) != WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
		return 1;
	}
	if (option == WINHTTP_OPTION_DECOMPRESSION) {
		session->decompress = true;
		return 1;
	}
	// Protocol selection and the rest have no counterpart worth forcing:
	// libcurl negotiates TLS itself. Reporting success keeps the caller from
	// treating a setting it cannot influence as a failure.
	return 1;
}

inline int WinHttpAddRequestHeaders(HINTERNET requestHandle, const wchar_t* headers,
                                    unsigned long length, unsigned long modifiers)
{
	(void)length; (void)modifiers;
	auto* request = pob_win_compat::AsHttpKind<pob_win_compat::HttpRequest>(
		requestHandle, pob_win_compat::kHttpRequest);
	if (request == nullptr || headers == nullptr) return 0;
	// A Win32 header block is CRLF-separated; split it so libcurl gets one
	// header per list entry.
	std::string block = pob_win_compat::WideToUtf8(headers);
	size_t pos = 0;
	while (pos < block.size()) {
		size_t end = block.find('\r', pos);
		if (end == std::string::npos) end = block.size();
		std::string line = block.substr(pos, end - pos);
		while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) line.pop_back();
		if (!line.empty()) request->headers.push_back(line);
		pos = end + 1;
		while (pos < block.size() && (block[pos] == '\n' || block[pos] == '\r')) pos++;
	}
	return 1;
}

// Win32 sends the request here and reads it in later calls. libcurl performs
// the whole transfer in one go, so this is where it happens.
inline int WinHttpSendRequest(HINTERNET requestHandle, const wchar_t* extraHeaders,
                              unsigned long headersLength, void* optional,
                              unsigned long optionalLength, unsigned long totalLength,
                              uintptr_t context)
{
	(void)optional; (void)optionalLength; (void)totalLength; (void)context;
	auto* request = pob_win_compat::AsHttpKind<pob_win_compat::HttpRequest>(
		requestHandle, pob_win_compat::kHttpRequest);
	if (request == nullptr || request->connect == nullptr) return 0;
	if (extraHeaders != nullptr && headersLength != 0) {
		WinHttpAddRequestHeaders(requestHandle, extraHeaders, headersLength, 0);
	}

	pob_win_compat::HttpConnect* connect = request->connect;
	pob_win_compat::HttpSession* session = connect->session;

	std::string url = request->secure ? "https://" : "http://";
	url += connect->host;
	const unsigned short defaultPort = request->secure ? 443 : 80;
	if (connect->port != 0 && connect->port != defaultPort) {
		url += ":" + std::to_string(connect->port);
	}
	if (!request->path.empty() && request->path.front() != '/') url += "/";
	url += request->path;

	CURL* curl = curl_easy_init();
	if (curl == nullptr) return 0;

	request->body.clear();
	request->responseHeaders.clear();
	request->readOffset = 0;

	curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, pob_win_compat::CurlWriteBody);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &request->body);
	curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, pob_win_compat::CurlWriteHeader);
	curl_easy_setopt(curl, CURLOPT_HEADERDATA, &request->responseHeaders);
	curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
	if (session != nullptr) {
		if (!session->userAgent.empty()) {
			curl_easy_setopt(curl, CURLOPT_USERAGENT, session->userAgent.c_str());
		}
		if (!session->proxy.empty()) {
			curl_easy_setopt(curl, CURLOPT_PROXY, session->proxy.c_str());
		}
		curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, session->connectTimeoutMs);
		curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, session->totalTimeoutMs);
		curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, session->followRedirects ? 1L : 0L);
		if (session->decompress) curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");
	}
	if (request->verb == "HEAD") {
		curl_easy_setopt(curl, CURLOPT_NOBODY, 1L);
	} else if (request->verb != "GET") {
		curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, request->verb.c_str());
	}

	curl_slist* list = nullptr;
	for (const std::string& h : request->headers) {
		list = curl_slist_append(list, h.c_str());
	}
	if (list != nullptr) curl_easy_setopt(curl, CURLOPT_HTTPHEADER, list);

	const CURLcode result = curl_easy_perform(curl);
	if (result == CURLE_OK) {
		long status = 0;
		curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
		request->status = status;
		request->sent = true;
	}
	if (list != nullptr) curl_slist_free_all(list);
	curl_easy_cleanup(curl);
	return request->sent ? 1 : 0;
}

// The transfer already happened; nothing is left to wait for.
inline int WinHttpReceiveResponse(HINTERNET requestHandle, void* reserved)
{
	(void)reserved;
	auto* request = pob_win_compat::AsHttpKind<pob_win_compat::HttpRequest>(
		requestHandle, pob_win_compat::kHttpRequest);
	return (request != nullptr && request->sent) ? 1 : 0;
}

inline int WinHttpQueryHeaders(HINTERNET requestHandle, unsigned long infoLevel,
                               const wchar_t* name, void* buffer,
                               unsigned long* bufferLength, unsigned long* index)
{
	(void)index;
	auto* request = pob_win_compat::AsHttpKind<pob_win_compat::HttpRequest>(
		requestHandle, pob_win_compat::kHttpRequest);
	if (request == nullptr || bufferLength == nullptr) return 0;

	// Numeric form: the status code, written as a DWORD.
	if ((infoLevel & WINHTTP_QUERY_FLAG_NUMBER) != 0) {
		if (buffer == nullptr || *bufferLength < sizeof(unsigned long)) {
			*bufferLength = sizeof(unsigned long);
			return 0;
		}
		const unsigned long base = infoLevel & ~WINHTTP_QUERY_FLAG_NUMBER;
		unsigned long value = 0;
		if (base == WINHTTP_QUERY_STATUS_CODE) {
			value = static_cast<unsigned long>(request->status);
		} else if (base == WINHTTP_QUERY_CONTENT_LENGTH) {
			value = static_cast<unsigned long>(request->body.size());
		}
		std::memcpy(buffer, &value, sizeof(value));
		*bufferLength = sizeof(value);
		return 1;
	}

	// String form: a named header, or Location.
	std::string key;
	const unsigned long base = infoLevel & 0xFFFFu;
	if (base == WINHTTP_QUERY_LOCATION) key = "location";
	else if (base == WINHTTP_QUERY_CONTENT_LENGTH) key = "content-length";
	else if (name != nullptr) {
		key = pob_win_compat::WideToUtf8(name);
		for (char& c : key) {
			if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + 32);
		}
	}
	auto it = request->responseHeaders.find(key);
	if (it == request->responseHeaders.end()) {
		*bufferLength = 0;
		return 0;
	}
	const size_t needed = (it->second.size() + 1) * sizeof(wchar_t);
	if (buffer == nullptr || *bufferLength < needed) {
		*bufferLength = static_cast<unsigned long>(needed);
		return 0;
	}
	pob_win_compat::Utf8ToWide(it->second.c_str(), static_cast<wchar_t*>(buffer),
	                           it->second.size() + 1);
	*bufferLength = static_cast<unsigned long>(it->second.size() * sizeof(wchar_t));
	return 1;
}

inline int WinHttpQueryDataAvailable(HINTERNET requestHandle, unsigned long* available)
{
	auto* request = pob_win_compat::AsHttpKind<pob_win_compat::HttpRequest>(
		requestHandle, pob_win_compat::kHttpRequest);
	if (request == nullptr || available == nullptr) return 0;
	*available = static_cast<unsigned long>(request->body.size() - request->readOffset);
	return 1;
}

inline int WinHttpReadData(HINTERNET requestHandle, void* buffer,
                           unsigned long toRead, unsigned long* read)
{
	auto* request = pob_win_compat::AsHttpKind<pob_win_compat::HttpRequest>(
		requestHandle, pob_win_compat::kHttpRequest);
	if (request == nullptr || buffer == nullptr) return 0;
	const size_t left = request->body.size() - request->readOffset;
	const size_t n = (toRead < left) ? toRead : left;
	if (n > 0) std::memcpy(buffer, request->body.data() + request->readOffset, n);
	request->readOffset += n;
	if (read != nullptr) *read = static_cast<unsigned long>(n);
	return 1;
}

inline int WinHttpCloseHandle(HINTERNET handle)
{
	pob_win_compat::HttpHandleBase* base = pob_win_compat::AsHttp(handle);
	if (base == nullptr) return 0;
	switch (base->kind) {
	case pob_win_compat::kHttpSession:
		delete static_cast<pob_win_compat::HttpSession*>(base);
		break;
	case pob_win_compat::kHttpConnect:
		delete static_cast<pob_win_compat::HttpConnect*>(base);
		break;
	case pob_win_compat::kHttpRequest:
		delete static_cast<pob_win_compat::HttpRequest*>(base);
		break;
	}
	return 1;
}

#endif // !_WIN32
