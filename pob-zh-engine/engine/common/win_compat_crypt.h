// SimpleGraphic Engine
//
// DPAPI shim for non-Windows targets, implemented over the macOS keychain.
//
// On Windows the launcher protects two credentials with CryptProtectData --
// the trade-site session id and the GitHub token -- and writes the resulting
// blob to disk. That API encrypts with a key bound to this machine and this
// user, so a copied file is useless elsewhere; the callers rely on exactly
// that and treat a failed decrypt as "nothing saved".
//
// The keychain gives the same guarantee: an item in the login keychain is
// readable only by this user on this machine. The shape differs, though --
// DPAPI hands back a blob for the caller to store, while the keychain stores
// the secret itself. So the "blob" produced here is a reference, and the
// secret never reaches the file the caller writes. That is a stronger
// position than DPAPI's, not a weaker one: the file holds no ciphertext to
// attack offline.
//
// The entropy argument, which Windows uses to separate one use of DPAPI from
// another, becomes part of the keychain account name so the two credentials
// stay separate here too.

#pragma once

#if !defined(_WIN32)

#include "win_compat.h"

#include <string>
#include <vector>

#if defined(__APPLE__)
#include <CoreFoundation/CoreFoundation.h>
#include <Security/Security.h>
#endif

#if !defined(CRYPTPROTECT_UI_FORBIDDEN)
#define CRYPTPROTECT_UI_FORBIDDEN 0x1u
#endif
#if !defined(CRYPTPROTECT_LOCAL_MACHINE)
#define CRYPTPROTECT_LOCAL_MACHINE 0x4u
#endif

struct DATA_BLOB {
	uint32_t cbData;
	unsigned char* pbData;
};

namespace pob_win_compat {

inline const char* KeychainService() { return "PobTools"; }

// The reference written in place of DPAPI's ciphertext. The tag makes a blob
// from this platform recognisable, and the account follows it.
inline const char* KeychainBlobPrefix() { return "keychain-v1:"; }

// Turns the entropy bytes into a stable, printable account name. The callers
// pass a wide string literal, so the bytes are interpreted as one when they
// look like one and hex-encoded otherwise.
inline std::string AccountFromEntropy(const DATA_BLOB* entropy)
{
	if (entropy == nullptr || entropy->pbData == nullptr || entropy->cbData == 0) {
		return std::string("default");
	}
	const size_t wideCount = entropy->cbData / sizeof(wchar_t);
	if (wideCount > 0) {
		const wchar_t* wide = reinterpret_cast<const wchar_t*>(entropy->pbData);
		bool printable = true;
		std::string out;
		for (size_t i = 0; i < wideCount; i++) {
			if (wide[i] == L'\0') break;
			if (wide[i] < 0x20 || wide[i] > 0x7E) { printable = false; break; }
			out.push_back(static_cast<char>(wide[i]));
		}
		if (printable && !out.empty()) return out;
	}
	static const char* digits = "0123456789abcdef";
	std::string hex;
	hex.reserve(entropy->cbData * 2);
	for (unsigned long i = 0; i < entropy->cbData; i++) {
		hex.push_back(digits[entropy->pbData[i] >> 4]);
		hex.push_back(digits[entropy->pbData[i] & 0x0F]);
	}
	return hex;
}

#if defined(__APPLE__)

inline bool KeychainStore(const std::string& account, const unsigned char* data, size_t size)
{
	CFStringRef service = CFStringCreateWithCString(nullptr, KeychainService(), kCFStringEncodingUTF8);
	CFStringRef acct = CFStringCreateWithCString(nullptr, account.c_str(), kCFStringEncodingUTF8);
	CFDataRef value = CFDataCreate(nullptr, data, static_cast<CFIndex>(size));
	if (service == nullptr || acct == nullptr || value == nullptr) {
		if (service) CFRelease(service);
		if (acct) CFRelease(acct);
		if (value) CFRelease(value);
		return false;
	}

	const void* queryKeys[] = { kSecClass, kSecAttrService, kSecAttrAccount };
	const void* queryVals[] = { kSecClassGenericPassword, service, acct };
	CFDictionaryRef query = CFDictionaryCreate(nullptr, queryKeys, queryVals, 3,
	                                           &kCFTypeDictionaryKeyCallBacks,
	                                           &kCFTypeDictionaryValueCallBacks);

	// Replace an existing item rather than failing on duplicate.
	const void* updKeys[] = { kSecValueData };
	const void* updVals[] = { value };
	CFDictionaryRef update = CFDictionaryCreate(nullptr, updKeys, updVals, 1,
	                                            &kCFTypeDictionaryKeyCallBacks,
	                                            &kCFTypeDictionaryValueCallBacks);

	OSStatus status = SecItemUpdate(query, update);
	if (status == errSecItemNotFound) {
		const void* addKeys[] = { kSecClass, kSecAttrService, kSecAttrAccount,
		                          kSecValueData, kSecAttrAccessible };
		const void* addVals[] = { kSecClassGenericPassword, service, acct, value,
		                          kSecAttrAccessibleWhenUnlocked };
		CFDictionaryRef add = CFDictionaryCreate(nullptr, addKeys, addVals, 5,
		                                         &kCFTypeDictionaryKeyCallBacks,
		                                         &kCFTypeDictionaryValueCallBacks);
		status = SecItemAdd(add, nullptr);
		CFRelease(add);
	}

	CFRelease(update);
	CFRelease(query);
	CFRelease(value);
	CFRelease(acct);
	CFRelease(service);
	return status == errSecSuccess;
}

inline bool KeychainLoad(const std::string& account, std::vector<unsigned char>& out)
{
	out.clear();
	CFStringRef service = CFStringCreateWithCString(nullptr, KeychainService(), kCFStringEncodingUTF8);
	CFStringRef acct = CFStringCreateWithCString(nullptr, account.c_str(), kCFStringEncodingUTF8);
	if (service == nullptr || acct == nullptr) {
		if (service) CFRelease(service);
		if (acct) CFRelease(acct);
		return false;
	}

	const void* keys[] = { kSecClass, kSecAttrService, kSecAttrAccount,
	                       kSecReturnData, kSecMatchLimit };
	const void* vals[] = { kSecClassGenericPassword, service, acct,
	                       kCFBooleanTrue, kSecMatchLimitOne };
	CFDictionaryRef query = CFDictionaryCreate(nullptr, keys, vals, 5,
	                                           &kCFTypeDictionaryKeyCallBacks,
	                                           &kCFTypeDictionaryValueCallBacks);
	CFTypeRef result = nullptr;
	const OSStatus status = SecItemCopyMatching(query, &result);
	CFRelease(query);
	CFRelease(acct);
	CFRelease(service);

	if (status != errSecSuccess || result == nullptr) return false;
	if (CFGetTypeID(result) == CFDataGetTypeID()) {
		CFDataRef data = static_cast<CFDataRef>(result);
		const UInt8* bytes = CFDataGetBytePtr(data);
		const CFIndex length = CFDataGetLength(data);
		out.assign(bytes, bytes + length);
	}
	CFRelease(result);
	return !out.empty();
}

#else // non-Apple POSIX

// No system credential store is assumed here. Refusing is the honest answer:
// the callers treat a failure as "nothing saved", which keeps the credential
// out of a plaintext file rather than silently weakening it.
inline bool KeychainStore(const std::string&, const unsigned char*, size_t) { return false; }
inline bool KeychainLoad(const std::string&, std::vector<unsigned char>&) { return false; }

#endif

// DPAPI's output is freed with LocalFree; the allocations below come from
// malloc so that mapping holds.
inline bool FillBlob(DATA_BLOB* out, const std::string& bytes)
{
	if (out == nullptr) return false;
	out->pbData = static_cast<unsigned char*>(std::malloc(bytes.size() ? bytes.size() : 1));
	if (out->pbData == nullptr) return false;
	if (!bytes.empty()) std::memcpy(out->pbData, bytes.data(), bytes.size());
	out->cbData = static_cast<unsigned long>(bytes.size());
	return true;
}

} // namespace pob_win_compat

// Win32: encrypts with a machine-and-user bound key. Here the secret goes to
// the keychain and the "blob" is the reference that locates it again.
inline int CryptProtectData(DATA_BLOB* in, const wchar_t* description,
                            DATA_BLOB* entropy, void* reserved, void* prompt,
                            unsigned long flags, DATA_BLOB* out)
{
	(void)description; (void)reserved; (void)prompt; (void)flags;
	if (in == nullptr || in->pbData == nullptr || out == nullptr) return 0;
	const std::string account = pob_win_compat::AccountFromEntropy(entropy);
	if (!pob_win_compat::KeychainStore(account, in->pbData, in->cbData)) return 0;
	const std::string reference = std::string(pob_win_compat::KeychainBlobPrefix()) + account;
	return pob_win_compat::FillBlob(out, reference) ? 1 : 0;
}

// Win32: the inverse. A blob this platform did not write, or an item the
// keychain will not hand over, both land on the caller's "not saved" path.
inline int CryptUnprotectData(DATA_BLOB* in, wchar_t** description,
                              DATA_BLOB* entropy, void* reserved, void* prompt,
                              unsigned long flags, DATA_BLOB* out)
{
	(void)reserved; (void)prompt; (void)flags;
	if (description != nullptr) *description = nullptr;
	if (in == nullptr || in->pbData == nullptr || out == nullptr) return 0;

	const std::string blob(reinterpret_cast<const char*>(in->pbData), in->cbData);
	const std::string prefix = pob_win_compat::KeychainBlobPrefix();
	std::string account;
	if (blob.compare(0, prefix.size(), prefix) == 0) {
		account = blob.substr(prefix.size());
	} else {
		// Written by the Windows build, or by an older layout: there is no key
		// here that could decrypt it.
		return 0;
	}
	// The entropy must agree with what the reference says, so one use of this
	// API cannot read another's secret.
	if (account != pob_win_compat::AccountFromEntropy(entropy)) return 0;

	std::vector<unsigned char> secret;
	if (!pob_win_compat::KeychainLoad(account, secret)) return 0;
	const std::string bytes(reinterpret_cast<const char*>(secret.data()), secret.size());
	return pob_win_compat::FillBlob(out, bytes) ? 1 : 0;
}

#endif // !_WIN32
