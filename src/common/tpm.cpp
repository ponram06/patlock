// TPM-backed key protection for Patlock Phase 5 hardening.
//
// Uses the Microsoft Platform Crypto Provider (MS_PLATFORM_CRYPTO_PROVIDER)
// to manage a hardware TPM-backed RSA 2048 key. The key's usage is bound
// to a pattern-derived authorization value (PCP_USAGEAUTH).
//
// The symmetric AES-256 key used to encrypt the Windows password is wrapped
// with RSA-OAEP (SHA-256) inside the TPM. Decryption requires the TPM hardware
// and the correct pattern hash. Failed attempts are rate-limited by the TPM's
// hardware anti-hammering dictionary attack logic.
//
// Falls back to baseline mode if the TPM or Platform Crypto Provider is unavailable.
//
// Sources checked:
// - Microsoft Platform Crypto Provider: https://learn.microsoft.com/en-us/windows/win32/seccng/cng-key-storage-providers
// - NCryptCreatePersistedKey: https://learn.microsoft.com/en-us/windows/win32/api/ncrypt/nf-ncrypt-ncryptcreatepersistedkey
// - NCryptEncrypt / NCryptDecrypt: https://learn.microsoft.com/en-us/windows/win32/api/ncrypt/nf-ncrypt-ncryptencrypt
// - BCRYPT_OAEP_PADDING_INFO: https://learn.microsoft.com/en-us/windows/win32/api/bcrypt/ns-bcrypt-bcrypt_oaep_padding_info

#include "tpm.h"
#include <bcrypt.h>
#include <ncrypt.h>
#include <strsafe.h>

#pragma comment(lib, "ncrypt.lib")

namespace patlock {
namespace tpm {
namespace {

constexpr size_t kMaxKeyName = 256;
constexpr LPCWSTR PCP_USAGEAUTH = L"PCP_USAGEAUTH";

bool MakeKeyName(PCWSTR sid, wchar_t (&name)[kMaxKeyName]) noexcept {
    return SUCCEEDED(StringCchPrintfW(name, kMaxKeyName, L"Patlock_%s", sid));
}

struct ProvHandle {
    NCRYPT_PROV_HANDLE h = 0;
    ~ProvHandle() { if (h) NCryptFreeObject(h); }
};

struct KeyHandle {
    NCRYPT_KEY_HANDLE h = 0;
    ~KeyHandle() { if (h) NCryptFreeObject(h); }
};

} // anon namespace

bool Available() noexcept {
    ProvHandle prov;
    SECURITY_STATUS status = NCryptOpenStorageProvider(
        &prov.h, MS_PLATFORM_CRYPTO_PROVIDER, 0);
    return status == ERROR_SUCCESS;
}

HRESULT ProtectKey(PCWSTR sid, const BYTE authValue[32], const BYTE plainKey[32],
                   BYTE wrappedKey[256]) noexcept {
    SecureZeroMemory(wrappedKey, 256);
    if (!sid || !authValue || !plainKey) return E_INVALIDARG;

    wchar_t keyName[kMaxKeyName]{};
    if (!MakeKeyName(sid, keyName)) return E_INVALIDARG;

    ProvHandle prov;
    SECURITY_STATUS status = NCryptOpenStorageProvider(
        &prov.h, MS_PLATFORM_CRYPTO_PROVIDER, 0);
    if (status != ERROR_SUCCESS) return HRESULT_FROM_WIN32(status);

    // Delete any old key with this name
    {
        KeyHandle oldKey;
        if (NCryptOpenKey(prov.h, &oldKey.h, keyName, 0, NCRYPT_MACHINE_KEY_FLAG) == ERROR_SUCCESS) {
            NCryptDeleteKey(oldKey.h, 0);
            oldKey.h = 0;
        }
    }

    KeyHandle key;
    status = NCryptCreatePersistedKey(
        prov.h, &key.h, BCRYPT_RSA_ALGORITHM, keyName, 0, NCRYPT_MACHINE_KEY_FLAG);
    if (status != ERROR_SUCCESS) return HRESULT_FROM_WIN32(status);

    DWORD keyLength = 2048;
    status = NCryptSetProperty(key.h, NCRYPT_LENGTH_PROPERTY,
        reinterpret_cast<PBYTE>(&keyLength), sizeof(keyLength), 0);
    if (status != ERROR_SUCCESS) return HRESULT_FROM_WIN32(status);

    // Set usage authorization to the pattern hash
    status = NCryptSetProperty(key.h, PCP_USAGEAUTH,
        const_cast<PBYTE>(authValue), 32, 0);
    if (status != ERROR_SUCCESS) {
        status = NCryptSetProperty(key.h, NCRYPT_PIN_PROPERTY,
            const_cast<PBYTE>(authValue), 32, 0);
    }
    if (status != ERROR_SUCCESS) return HRESULT_FROM_WIN32(status);

    status = NCryptFinalizeKey(key.h, 0);
    if (status != ERROR_SUCCESS) return HRESULT_FROM_WIN32(status);

    // Encrypt plainKey (32 bytes) with RSA-OAEP SHA-256
    BCRYPT_OAEP_PADDING_INFO padding{ BCRYPT_SHA256_ALGORITHM, nullptr, 0 };
    DWORD cbOutput = 0;
    status = NCryptEncrypt(key.h, const_cast<PBYTE>(plainKey), 32,
                           &padding, wrappedKey, 256, &cbOutput, NCRYPT_PAD_OAEP_FLAG);
    if (status != ERROR_SUCCESS || cbOutput != 256) {
        SecureZeroMemory(wrappedKey, 256);
        return FAILED(HRESULT_FROM_WIN32(status)) ? HRESULT_FROM_WIN32(status) : E_FAIL;
    }

    return S_OK;
}

HRESULT UnprotectKey(PCWSTR sid, const BYTE authValue[32], const BYTE wrappedKey[256],
                     BYTE plainKey[32]) noexcept {
    SecureZeroMemory(plainKey, 32);
    if (!sid || !authValue || !wrappedKey) return E_INVALIDARG;

    wchar_t keyName[kMaxKeyName]{};
    if (!MakeKeyName(sid, keyName)) return E_INVALIDARG;

    ProvHandle prov;
    SECURITY_STATUS status = NCryptOpenStorageProvider(
        &prov.h, MS_PLATFORM_CRYPTO_PROVIDER, 0);
    if (status != ERROR_SUCCESS) return HRESULT_FROM_WIN32(status);

    KeyHandle key;
    status = NCryptOpenKey(prov.h, &key.h, keyName, 0, NCRYPT_MACHINE_KEY_FLAG);
    if (status != ERROR_SUCCESS) return HRESULT_FROM_WIN32(status);

    // Supply usage authorization
    status = NCryptSetProperty(key.h, PCP_USAGEAUTH,
        const_cast<PBYTE>(authValue), 32, 0);
    if (status != ERROR_SUCCESS) {
        status = NCryptSetProperty(key.h, NCRYPT_PIN_PROPERTY,
            const_cast<PBYTE>(authValue), 32, 0);
    }
    if (status != ERROR_SUCCESS) return HRESULT_FROM_WIN32(status);

    // Decrypt wrappedKey with RSA-OAEP SHA-256
    BCRYPT_OAEP_PADDING_INFO padding{ BCRYPT_SHA256_ALGORITHM, nullptr, 0 };
    DWORD cbOutput = 0;
    status = NCryptDecrypt(key.h, const_cast<PBYTE>(wrappedKey), 256,
                           &padding, plainKey, 32, &cbOutput, NCRYPT_PAD_OAEP_FLAG);
    if (status != ERROR_SUCCESS || cbOutput != 32) {
        SecureZeroMemory(plainKey, 32);
        return FAILED(HRESULT_FROM_WIN32(status)) ? HRESULT_FROM_WIN32(status) : E_ACCESSDENIED;
    }

    return S_OK;
}

HRESULT DeleteKey(PCWSTR sid) noexcept {
    if (!sid) return E_INVALIDARG;

    wchar_t keyName[kMaxKeyName]{};
    if (!MakeKeyName(sid, keyName)) return E_INVALIDARG;

    ProvHandle prov;
    SECURITY_STATUS status = NCryptOpenStorageProvider(
        &prov.h, MS_PLATFORM_CRYPTO_PROVIDER, 0);
    if (status != ERROR_SUCCESS) return HRESULT_FROM_WIN32(status);

    KeyHandle key;
    status = NCryptOpenKey(prov.h, &key.h, keyName, 0, NCRYPT_MACHINE_KEY_FLAG);
    if (status == ERROR_SUCCESS) {
        NCryptDeleteKey(key.h, 0);
        key.h = 0;
    }
    return S_OK;
}

} // namespace tpm
} // namespace patlock
