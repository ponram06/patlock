// TPM wrapping through the Microsoft Platform Crypto Provider.
// API sources and outstanding VM/hardware checks: docs/TPM_VALIDATION.md.
#include "tpm.h"
#include <bcrypt.h>
#include <ncrypt.h>
#include <tbs.h>
#include <sddl.h>
#include <strsafe.h>

#pragma comment(lib, "ncrypt.lib")
#pragma comment(lib, "tbs.lib")
#pragma comment(lib, "advapi32.lib")

namespace patlock {
namespace tpm {
namespace {
constexpr size_t kMaxKeyName = 256;
HRESULT Status(SECURITY_STATUS value) noexcept {
    // Preserve NTE/TPM HRESULTs; only map positive Win32 status values.
    return value <= 0 ? static_cast<HRESULT>(value) : HRESULT_FROM_WIN32(value);
}
bool MakeKeyName(PCWSTR identity, wchar_t (&name)[kMaxKeyName]) noexcept {
    if (!identity || !*identity) return false;
    return SUCCEEDED(StringCchPrintfW(name, kMaxKeyName, L"Patlock_%s", identity));
}
struct ProvHandle {
    NCRYPT_PROV_HANDLE h = 0;
    ~ProvHandle() { if (h) NCryptFreeObject(h); }
};
struct KeyHandle {
    NCRYPT_KEY_HANDLE h = 0;
    ~KeyHandle() { if (h) NCryptFreeObject(h); }
    HRESULT Delete() noexcept {
        if (!h) return S_OK;
        const SECURITY_STATUS status = NCryptDeleteKey(h, NCRYPT_SILENT_FLAG);
        if (status == ERROR_SUCCESS) h = 0;
        return Status(status);
    }
};
struct AuthText {
    wchar_t text[65]{};
    explicit AuthText(const BYTE auth[32]) noexcept {
        constexpr wchar_t hex[] = L"0123456789abcdef";
        for (size_t i = 0; i < 32; ++i) {
            text[2 * i] = hex[auth[i] >> 4];
            text[2 * i + 1] = hex[auth[i] & 15];
        }
    }
    ~AuthText() { SecureZeroMemory(text, sizeof(text)); }
};
HRESULT SetAuth(NCRYPT_KEY_HANDLE key, const BYTE auth[32]) noexcept {
    // PCPTool CreateKey/Decrypt use a terminated UTF-16 NCRYPT_PIN_PROPERTY.
    // Do not persist this property as application metadata.
    AuthText pin(auth);
    return Status(NCryptSetProperty(key, NCRYPT_PIN_PROPERTY,
        reinterpret_cast<PBYTE>(pin.text), sizeof(pin.text), NCRYPT_SILENT_FLAG));
}
HRESULT RestrictKey(NCRYPT_KEY_HANDLE key) noexcept {
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(
            L"D:P(A;;GA;;;SY)(A;;GA;;;BA)", SDDL_REVISION_1,
            &descriptor, nullptr)) return HRESULT_FROM_WIN32(GetLastError());
    const SECURITY_STATUS status = NCryptSetProperty(key, NCRYPT_SECURITY_DESCR_PROPERTY,
        static_cast<PBYTE>(descriptor), GetSecurityDescriptorLength(descriptor),
        DACL_SECURITY_INFORMATION | NCRYPT_SILENT_FLAG);
    LocalFree(descriptor);
    return Status(status);
}
HRESULT Decrypt(NCRYPT_KEY_HANDLE key, const BYTE wrapped[256], BYTE plain[32]) noexcept {
    BCRYPT_OAEP_PADDING_INFO padding{ BCRYPT_SHA256_ALGORITHM, nullptr, 0 };
    DWORD size = 0;
    const SECURITY_STATUS status = NCryptDecrypt(key, const_cast<PBYTE>(wrapped), 256,
        &padding, plain, 32, &size, NCRYPT_PAD_OAEP_FLAG | NCRYPT_SILENT_FLAG);
    if (status != ERROR_SUCCESS || size != 32) {
        SecureZeroMemory(plain, 32);
        return status != ERROR_SUCCESS ? Status(status) : HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }
    return S_OK;
}
} // namespace

HRESULT Probe() noexcept {
    TPM_DEVICE_INFO info{};
    const TBS_RESULT result = Tbsi_GetDeviceInfo(sizeof(info), &info);
    if (result == TBS_E_TPM_NOT_FOUND) return S_FALSE;
    if (result != TBS_SUCCESS) return static_cast<HRESULT>(result);
    ProvHandle provider;
    const SECURITY_STATUS status = NCryptOpenStorageProvider(
        &provider.h, MS_PLATFORM_CRYPTO_PROVIDER, 0);
    return Status(status);
}
bool Available() noexcept { return Probe() == S_OK; }

HRESULT ProtectKey(PCWSTR identity, const BYTE authValue[32], const BYTE plainKey[32],
                   BYTE wrappedKey[256]) noexcept {
    if (!wrappedKey) return E_INVALIDARG;
    SecureZeroMemory(wrappedKey, 256);
    if (!authValue || !plainKey) return E_INVALIDARG;
    wchar_t keyName[kMaxKeyName]{};
    if (!MakeKeyName(identity, keyName)) return E_INVALIDARG;
    ProvHandle provider;
    SECURITY_STATUS status = NCryptOpenStorageProvider(
        &provider.h, MS_PLATFORM_CRYPTO_PROVIDER, 0);
    if (status != ERROR_SUCCESS) return Status(status);
    KeyHandle key;
    // No overwrite: the caller supplies an enrollment-generation identity.
    status = NCryptCreatePersistedKey(provider.h, &key.h, BCRYPT_RSA_ALGORITHM,
        keyName, 0, NCRYPT_MACHINE_KEY_FLAG);
    if (status != ERROR_SUCCESS) return Status(status);
    const auto create = [&]() noexcept -> HRESULT {
        DWORD length = 2048;
        SECURITY_STATUS current = NCryptSetProperty(key.h, NCRYPT_LENGTH_PROPERTY,
            reinterpret_cast<PBYTE>(&length), sizeof(length), NCRYPT_SILENT_FLAG);
        if (current != ERROR_SUCCESS) return Status(current);
        DWORD exportPolicy = 0;
        current = NCryptSetProperty(key.h, NCRYPT_EXPORT_POLICY_PROPERTY,
            reinterpret_cast<PBYTE>(&exportPolicy), sizeof(exportPolicy), NCRYPT_SILENT_FLAG);
        if (current != ERROR_SUCCESS) return Status(current);
        DWORD usage = NCRYPT_ALLOW_DECRYPT_FLAG;
        current = NCryptSetProperty(key.h, NCRYPT_KEY_USAGE_PROPERTY,
            reinterpret_cast<PBYTE>(&usage), sizeof(usage), NCRYPT_SILENT_FLAG);
        if (current != ERROR_SUCCESS) return Status(current);
        HRESULT hr = SetAuth(key.h, authValue);
        if (FAILED(hr)) return hr;
        current = NCryptFinalizeKey(key.h, NCRYPT_SILENT_FLAG);
        if (current != ERROR_SUCCESS) return Status(current);
        hr = RestrictKey(key.h);
        if (FAILED(hr)) return hr;
        BCRYPT_OAEP_PADDING_INFO padding{ BCRYPT_SHA256_ALGORITHM, nullptr, 0 };
        DWORD size = 0;
        current = NCryptEncrypt(key.h, const_cast<PBYTE>(plainKey), 32, &padding,
            wrappedKey, 256, &size, NCRYPT_PAD_OAEP_FLAG | NCRYPT_SILENT_FLAG);
        if (current != ERROR_SUCCESS) return Status(current);
        if (size != 256) return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        // Reopen to exercise persisted authorization rather than the creating handle.
        KeyHandle reopened;
        current = NCryptOpenKey(provider.h, &reopened.h, keyName, 0,
            NCRYPT_MACHINE_KEY_FLAG | NCRYPT_SILENT_FLAG);
        if (current != ERROR_SUCCESS) return Status(current);
        hr = SetAuth(reopened.h, authValue);
        if (FAILED(hr)) return hr;
        BYTE recovered[32]{};
        hr = Decrypt(reopened.h, wrappedKey, recovered);
        BYTE difference = 0;
        for (size_t i = 0; i < sizeof(recovered); ++i) difference = static_cast<BYTE>(difference | (recovered[i] ^ plainKey[i]));
        SecureZeroMemory(recovered, sizeof(recovered));
        if (FAILED(hr)) return hr;
        return difference == 0 ? S_OK : HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    };
    const HRESULT hr = create();
    if (FAILED(hr)) {
        SecureZeroMemory(wrappedKey, 256);
        // If deletion fails, propagate that failure; never report success/fallback.
        const HRESULT cleanup = key.Delete();
        return FAILED(cleanup) ? cleanup : hr;
    }
    return S_OK;
}
HRESULT UnprotectKey(PCWSTR identity, const BYTE authValue[32], const BYTE wrappedKey[256],
                     BYTE plainKey[32]) noexcept {
    if (!plainKey) return E_INVALIDARG;
    SecureZeroMemory(plainKey, 32);
    if (!authValue || !wrappedKey) return E_INVALIDARG;
    wchar_t keyName[kMaxKeyName]{};
    if (!MakeKeyName(identity, keyName)) return E_INVALIDARG;
    ProvHandle provider;
    SECURITY_STATUS status = NCryptOpenStorageProvider(&provider.h, MS_PLATFORM_CRYPTO_PROVIDER, 0);
    if (status != ERROR_SUCCESS) return Status(status);
    KeyHandle key;
    status = NCryptOpenKey(provider.h, &key.h, keyName, 0, NCRYPT_MACHINE_KEY_FLAG | NCRYPT_SILENT_FLAG);
    if (status != ERROR_SUCCESS) return Status(status);
    const HRESULT hr = SetAuth(key.h, authValue);
    if (FAILED(hr)) return hr;
    return Decrypt(key.h, wrappedKey, plainKey);
}
HRESULT DeleteKey(PCWSTR identity) noexcept {
    wchar_t keyName[kMaxKeyName]{};
    if (!MakeKeyName(identity, keyName)) return E_INVALIDARG;
    ProvHandle provider;
    SECURITY_STATUS status = NCryptOpenStorageProvider(&provider.h, MS_PLATFORM_CRYPTO_PROVIDER, 0);
    if (status != ERROR_SUCCESS) return Status(status);
    KeyHandle key;
    status = NCryptOpenKey(provider.h, &key.h, keyName, 0, NCRYPT_MACHINE_KEY_FLAG | NCRYPT_SILENT_FLAG);
    if (status == NTE_BAD_KEYSET || status == NTE_NOT_FOUND) return S_FALSE;
    if (status != ERROR_SUCCESS) return Status(status);
    return key.Delete();
}
} // namespace tpm
} // namespace patlock
