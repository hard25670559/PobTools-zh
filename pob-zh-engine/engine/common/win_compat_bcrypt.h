// SimpleGraphic Engine
//
// BCrypt shim for non-Windows targets: SHA-256 and ECDSA P-256 verification.
//
// The launcher uses these for one thing that matters: checking that an update
// was published by the maintainer. host/sig_verify.cpp hashes the manifest,
// imports a P-256 public key compiled into the binary, and verifies a raw
// r||s signature against it. Getting that wrong either rejects every genuine
// update or accepts a forged one, so this is implemented against the
// platform's own crypto rather than stubbed.
//
//   SHA-256                  -> CommonCrypto
//   P-256 key import         -> SecKeyCreateWithData, uncompressed EC point
//   ECDSA verify             -> SecKeyVerifySignature
//
// Two format differences have to be bridged:
//
//   * A BCRYPT_ECCPUBLIC_BLOB is a small header followed by X||Y. Security
//     wants the uncompressed point form, which is 0x04 followed by the same
//     X||Y, so the header is dropped and one byte prepended.
//   * BCrypt verifies a raw 64-byte r||s. Security expects the X9.62 DER
//     encoding, so the pair is wrapped in a SEQUENCE of two INTEGERs, with
//     the sign-bit padding DER requires.

#pragma once

#if !defined(_WIN32)

#include "win_compat.h"

#include <cstring>
#include <string>
#include <vector>

#if defined(__APPLE__)
#include <CommonCrypto/CommonDigest.h>
#include <CoreFoundation/CoreFoundation.h>
#include <Security/Security.h>
#endif

using NTSTATUS = long;
using UCHAR = unsigned char;
using PUCHAR = unsigned char*;
using ULONG = uint32_t;

using BCRYPT_ALG_HANDLE = void*;
using BCRYPT_HASH_HANDLE = void*;
using BCRYPT_KEY_HANDLE = void*;

#if !defined(STATUS_SUCCESS)
#define STATUS_SUCCESS ((NTSTATUS)0)
#endif
#if !defined(STATUS_INVALID_SIGNATURE)
#define STATUS_INVALID_SIGNATURE ((NTSTATUS)0xC000A000L)
#endif
#if !defined(STATUS_UNSUCCESSFUL)
#define STATUS_UNSUCCESSFUL ((NTSTATUS)0xC0000001L)
#endif

#if !defined(BCRYPT_SHA256_ALGORITHM)
#define BCRYPT_SHA256_ALGORITHM L"SHA256"
#endif
#if !defined(BCRYPT_ECDSA_P256_ALGORITHM)
#define BCRYPT_ECDSA_P256_ALGORITHM L"ECDSA_P256"
#endif
#if !defined(BCRYPT_ECCPUBLIC_BLOB)
#define BCRYPT_ECCPUBLIC_BLOB L"ECCPUBLICBLOB"
#endif
#if !defined(BCRYPT_OBJECT_LENGTH)
#define BCRYPT_OBJECT_LENGTH L"ObjectLength"
#endif
#if !defined(BCRYPT_HASH_LENGTH)
#define BCRYPT_HASH_LENGTH L"HashDigestLength"
#endif
#if !defined(BCRYPT_ECDSA_PUBLIC_P256_MAGIC)
#define BCRYPT_ECDSA_PUBLIC_P256_MAGIC 0x31534345u
#endif

// A BCRYPT_ECCPUBLIC_BLOB: the header, then X and Y of the public point.
struct BCRYPT_ECCKEY_BLOB {
	uint32_t dwMagic;
	uint32_t cbKey;
};

namespace pob_win_compat {

// ------------------------------------------------------------- SHA-256

struct ShaState {
	static constexpr unsigned int kMagic = 0x53484131u; // "SHA1"
	unsigned int magic = kMagic;
#if defined(__APPLE__)
	CC_SHA256_CTX ctx;
#endif
};

struct ShaAlgState {
	static constexpr unsigned int kMagic = 0x53414C31u; // "SAL1"
	unsigned int magic = kMagic;
};

// ----------------------------------------------------------- ECDSA key

struct EccKeyState {
	static constexpr unsigned int kMagic = 0x45434B31u; // "ECK1"
	unsigned int magic = kMagic;
	std::vector<unsigned char> point;   // 0x04 || X || Y
};

struct EcdsaAlgState {
	static constexpr unsigned int kMagic = 0x45414C31u; // "EAL1"
	unsigned int magic = kMagic;
};

template <typename T>
inline T* AsCrypto(void* h)
{
	const uintptr_t v = reinterpret_cast<uintptr_t>(h);
	if (h == nullptr || v < 0x10000) return nullptr;
	auto* candidate = static_cast<T*>(h);
	return candidate->magic == T::kMagic ? candidate : nullptr;
}

// Encodes one unsigned big-endian integer as a DER INTEGER, adding the leading
// zero DER requires when the top bit is set.
inline void DerAppendInteger(std::vector<unsigned char>& out,
                             const unsigned char* value, size_t size)
{
	size_t start = 0;
	while (start + 1 < size && value[start] == 0) start++;   // strip leading zeros
	const bool pad = (value[start] & 0x80) != 0;
	const size_t length = size - start + (pad ? 1 : 0);
	out.push_back(0x02);                                      // INTEGER
	out.push_back(static_cast<unsigned char>(length));        // P-256 fits one byte
	if (pad) out.push_back(0x00);
	out.insert(out.end(), value + start, value + size);
}

// Turns a raw r||s pair into the X9.62 DER sequence Security expects.
inline std::vector<unsigned char> RawEcdsaToDer(const unsigned char* raw, size_t size)
{
	std::vector<unsigned char> out;
	if (raw == nullptr || size == 0 || (size % 2) != 0) return out;
	const size_t half = size / 2;
	std::vector<unsigned char> body;
	DerAppendInteger(body, raw, half);
	DerAppendInteger(body, raw + half, half);
	out.push_back(0x30);                                      // SEQUENCE
	out.push_back(static_cast<unsigned char>(body.size()));
	out.insert(out.end(), body.begin(), body.end());
	return out;
}

} // namespace pob_win_compat

// ----------------------------------------------------------------- API

inline NTSTATUS BCryptOpenAlgorithmProvider(BCRYPT_ALG_HANDLE* handle, const wchar_t* algorithm,
                                            const wchar_t* implementation, unsigned long flags)
{
	(void)implementation; (void)flags;
	if (handle == nullptr || algorithm == nullptr) return STATUS_UNSUCCESSFUL;
	const std::string name = pob_win_compat::WideToUtf8(algorithm);
	if (name == "SHA256") {
		*handle = new pob_win_compat::ShaAlgState();
		return STATUS_SUCCESS;
	}
	if (name == "ECDSA_P256") {
		*handle = new pob_win_compat::EcdsaAlgState();
		return STATUS_SUCCESS;
	}
	*handle = nullptr;
	return STATUS_UNSUCCESSFUL;
}

inline NTSTATUS BCryptCloseAlgorithmProvider(BCRYPT_ALG_HANDLE handle, unsigned long flags)
{
	(void)flags;
	if (auto* sha = pob_win_compat::AsCrypto<pob_win_compat::ShaAlgState>(handle)) {
		delete sha;
		return STATUS_SUCCESS;
	}
	if (auto* ec = pob_win_compat::AsCrypto<pob_win_compat::EcdsaAlgState>(handle)) {
		delete ec;
		return STATUS_SUCCESS;
	}
	return STATUS_UNSUCCESSFUL;
}

// Win32 asks for buffer sizes before creating a hash; here nothing has to be
// pre-allocated, so the answers are the fixed sizes of SHA-256.
inline NTSTATUS BCryptGetProperty(void* handle, const wchar_t* property, unsigned char* output,
                                  unsigned long size, uint32_t* written, unsigned long flags)
{
	(void)handle; (void)flags;
	if (property == nullptr || written == nullptr) return STATUS_UNSUCCESSFUL;
	const std::string name = pob_win_compat::WideToUtf8(property);
	unsigned long value = 0;
	if (name == "ObjectLength") value = static_cast<unsigned long>(sizeof(pob_win_compat::ShaState));
	else if (name == "HashDigestLength") value = 32u;
	else return STATUS_UNSUCCESSFUL;
	*written = sizeof(value);
	if (output == nullptr || size < sizeof(value)) return STATUS_SUCCESS;
	std::memcpy(output, &value, sizeof(value));
	return STATUS_SUCCESS;
}

inline NTSTATUS BCryptCreateHash(BCRYPT_ALG_HANDLE algorithm, BCRYPT_HASH_HANDLE* handle,
                                 unsigned char* object, unsigned long objectSize,
                                 unsigned char* secret, unsigned long secretSize,
                                 unsigned long flags)
{
	(void)object; (void)objectSize; (void)secret; (void)secretSize; (void)flags;
	if (handle == nullptr) return STATUS_UNSUCCESSFUL;
	if (pob_win_compat::AsCrypto<pob_win_compat::ShaAlgState>(algorithm) == nullptr) {
		return STATUS_UNSUCCESSFUL;
	}
	auto* state = new pob_win_compat::ShaState();
#if defined(__APPLE__)
	CC_SHA256_Init(&state->ctx);
#endif
	*handle = state;
	return STATUS_SUCCESS;
}

inline NTSTATUS BCryptHashData(BCRYPT_HASH_HANDLE handle, unsigned char* data,
                               unsigned long size, unsigned long flags)
{
	(void)flags;
	auto* state = pob_win_compat::AsCrypto<pob_win_compat::ShaState>(handle);
	if (state == nullptr) return STATUS_UNSUCCESSFUL;
#if defined(__APPLE__)
	CC_SHA256_Update(&state->ctx, data, static_cast<CC_LONG>(size));
	return STATUS_SUCCESS;
#else
	(void)data; (void)size;
	return STATUS_UNSUCCESSFUL;
#endif
}

inline NTSTATUS BCryptFinishHash(BCRYPT_HASH_HANDLE handle, unsigned char* output,
                                 unsigned long size, unsigned long flags)
{
	(void)flags;
	auto* state = pob_win_compat::AsCrypto<pob_win_compat::ShaState>(handle);
	if (state == nullptr || output == nullptr || size < 32) return STATUS_UNSUCCESSFUL;
#if defined(__APPLE__)
	CC_SHA256_Final(output, &state->ctx);
	return STATUS_SUCCESS;
#else
	return STATUS_UNSUCCESSFUL;
#endif
}

inline NTSTATUS BCryptDestroyHash(BCRYPT_HASH_HANDLE handle)
{
	auto* state = pob_win_compat::AsCrypto<pob_win_compat::ShaState>(handle);
	if (state == nullptr) return STATUS_UNSUCCESSFUL;
	delete state;
	return STATUS_SUCCESS;
}

// Imports the public point out of a BCRYPT_ECCPUBLIC_BLOB.
inline NTSTATUS BCryptImportKeyPair(BCRYPT_ALG_HANDLE algorithm, BCRYPT_KEY_HANDLE unused,
                                    const wchar_t* blobType, BCRYPT_KEY_HANDLE* handle,
                                    unsigned char* input, unsigned long inputSize,
                                    unsigned long flags)
{
	(void)unused; (void)flags;
	if (handle == nullptr || input == nullptr) return STATUS_UNSUCCESSFUL;
	if (pob_win_compat::AsCrypto<pob_win_compat::EcdsaAlgState>(algorithm) == nullptr) {
		return STATUS_UNSUCCESSFUL;
	}
	if (blobType == nullptr || pob_win_compat::WideToUtf8(blobType) != "ECCPUBLICBLOB") {
		return STATUS_UNSUCCESSFUL;
	}
	if (inputSize < sizeof(BCRYPT_ECCKEY_BLOB)) return STATUS_UNSUCCESSFUL;

	BCRYPT_ECCKEY_BLOB header{};
	std::memcpy(&header, input, sizeof(header));
	const unsigned long coordinate = header.cbKey;
	if (coordinate == 0 || inputSize < sizeof(header) + coordinate * 2) {
		return STATUS_UNSUCCESSFUL;
	}

	auto* key = new pob_win_compat::EccKeyState();
	key->point.reserve(1 + coordinate * 2);
	key->point.push_back(0x04);   // uncompressed point
	key->point.insert(key->point.end(), input + sizeof(header),
	                  input + sizeof(header) + coordinate * 2);
	*handle = key;
	return STATUS_SUCCESS;
}

inline NTSTATUS BCryptDestroyKey(BCRYPT_KEY_HANDLE handle)
{
	auto* key = pob_win_compat::AsCrypto<pob_win_compat::EccKeyState>(handle);
	if (key == nullptr) return STATUS_UNSUCCESSFUL;
	delete key;
	return STATUS_SUCCESS;
}

// Verifies a raw r||s ECDSA signature over an already-computed digest.
inline NTSTATUS BCryptVerifySignature(BCRYPT_KEY_HANDLE handle, void* paddingInfo,
                                      unsigned char* digest, unsigned long digestSize,
                                      unsigned char* signature, unsigned long signatureSize,
                                      unsigned long flags)
{
	(void)paddingInfo; (void)flags;
	auto* key = pob_win_compat::AsCrypto<pob_win_compat::EccKeyState>(handle);
	if (key == nullptr || digest == nullptr || signature == nullptr) {
		return STATUS_UNSUCCESSFUL;
	}

#if defined(__APPLE__)
	const std::vector<unsigned char> der =
		pob_win_compat::RawEcdsaToDer(signature, signatureSize);
	if (der.empty()) return STATUS_INVALID_SIGNATURE;

	CFDataRef keyData = CFDataCreate(nullptr, key->point.data(),
	                                 static_cast<CFIndex>(key->point.size()));
	if (keyData == nullptr) return STATUS_UNSUCCESSFUL;

	const void* attrKeys[] = { kSecAttrKeyType, kSecAttrKeyClass };
	const void* attrVals[] = { kSecAttrKeyTypeECSECPrimeRandom, kSecAttrKeyClassPublic };
	CFDictionaryRef attrs = CFDictionaryCreate(nullptr, attrKeys, attrVals, 2,
	                                           &kCFTypeDictionaryKeyCallBacks,
	                                           &kCFTypeDictionaryValueCallBacks);
	CFErrorRef error = nullptr;
	SecKeyRef publicKey = SecKeyCreateWithData(keyData, attrs, &error);
	CFRelease(attrs);
	CFRelease(keyData);
	if (publicKey == nullptr) {
		if (error != nullptr) CFRelease(error);
		return STATUS_UNSUCCESSFUL;   // the key is not a point on the curve
	}

	CFDataRef digestData = CFDataCreate(nullptr, digest, static_cast<CFIndex>(digestSize));
	CFDataRef sigData = CFDataCreate(nullptr, der.data(), static_cast<CFIndex>(der.size()));
	bool ok = false;
	if (digestData != nullptr && sigData != nullptr) {
		ok = SecKeyVerifySignature(publicKey,
		                           kSecKeyAlgorithmECDSASignatureDigestX962SHA256,
		                           digestData, sigData, &error);
	}
	if (digestData != nullptr) CFRelease(digestData);
	if (sigData != nullptr) CFRelease(sigData);
	if (error != nullptr) CFRelease(error);
	CFRelease(publicKey);
	return ok ? STATUS_SUCCESS : STATUS_INVALID_SIGNATURE;
#else
	(void)digestSize; (void)signatureSize;
	// Without a verifier, refusing is the only safe answer: reporting success
	// would accept an unsigned update.
	return STATUS_UNSUCCESSFUL;
#endif
}

// ------------------------------------------- key generation / signing

// These exist for the maintainer-side tool that creates the signing key and
// signs a release. Verification is what the shipped launcher does, and that is
// implemented above; generating a key here would need a private-key store this
// shim deliberately does not open, so these report failure rather than
// producing something that looks like a key but is not protected.

inline NTSTATUS BCryptGenerateKeyPair(BCRYPT_ALG_HANDLE algorithm, BCRYPT_KEY_HANDLE* handle,
                                      unsigned long length, unsigned long flags)
{
	(void)algorithm; (void)length; (void)flags;
	if (handle != nullptr) *handle = nullptr;
	return STATUS_UNSUCCESSFUL;
}

inline NTSTATUS BCryptFinalizeKeyPair(BCRYPT_KEY_HANDLE handle, unsigned long flags)
{
	(void)handle; (void)flags;
	return STATUS_UNSUCCESSFUL;
}

inline NTSTATUS BCryptExportKey(BCRYPT_KEY_HANDLE key, BCRYPT_KEY_HANDLE exportKey,
                                const wchar_t* blobType, unsigned char* output,
                                unsigned long outputSize, uint32_t* written,
                                unsigned long flags)
{
	(void)key; (void)exportKey; (void)blobType; (void)output; (void)outputSize; (void)flags;
	if (written != nullptr) *written = 0;
	return STATUS_UNSUCCESSFUL;
}

inline NTSTATUS BCryptSignHash(BCRYPT_KEY_HANDLE key, void* paddingInfo,
                               unsigned char* digest, unsigned long digestSize,
                               unsigned char* signature, unsigned long signatureSize,
                               uint32_t* written, unsigned long flags)
{
	(void)key; (void)paddingInfo; (void)digest; (void)digestSize;
	(void)signature; (void)signatureSize; (void)flags;
	if (written != nullptr) *written = 0;
	return STATUS_UNSUCCESSFUL;
}

#endif // !_WIN32
