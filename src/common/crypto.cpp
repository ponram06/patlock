#include "security.h"
#include <bcrypt.h>
#include <sddl.h>
#include <strsafe.h>

namespace patlock {
bool Equal(const BYTE* left, const BYTE* right, size_t length) noexcept {
    volatile BYTE difference = 0;
    for (size_t i = 0; i < length; ++i) difference = static_cast<BYTE>(difference | (left[i] ^ right[i]));
    return difference == 0;
}
HRESULT Random(BYTE* output, ULONG length) noexcept {
    return BCryptGenRandom(nullptr, output, length, BCRYPT_USE_SYSTEM_PREFERRED_RNG) >= 0 ? S_OK : E_FAIL;
}
HRESULT HashPattern(const Pattern& pattern, const BYTE salt[16], BYTE hash[32]) noexcept {
    if (!pattern.Valid()) return E_INVALIDARG;
    Secret<BYTE, 9> encoded;
    for (size_t i = 0; i < pattern.count; ++i) encoded.value[i] = static_cast<BYTE>('0' + pattern.dots[i]);
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, BCRYPT_ALG_HANDLE_HMAC_FLAG) < 0) return E_FAIL;
    const NTSTATUS result = BCryptDeriveKeyPBKDF2(algorithm, encoded.data(), static_cast<ULONG>(pattern.count),
        const_cast<BYTE*>(salt), 16, HashIterations, hash, 32, 0);
    BCryptCloseAlgorithmProvider(algorithm, 0);
    return result >= 0 ? S_OK : E_FAIL;
}
HRESULT ValidatePassword(PCWSTR user, PCWSTR password, wchar_t (&sid)[184]) noexcept {
    sid[0] = L'\0';
    if (!user || !*user || !password || !*password || wcschr(user, L'\\') || wcschr(user, L'@')) return E_INVALIDARG;
    HANDLE token = nullptr;
    if (!LogonUserW(user, L".", password, LOGON32_LOGON_INTERACTIVE, LOGON32_PROVIDER_DEFAULT, &token))
        return HRESULT_FROM_WIN32(GetLastError());
    alignas(TOKEN_USER) BYTE buffer[sizeof(TOKEN_USER) + SECURITY_MAX_SID_SIZE]{};
    DWORD needed = 0;
    HRESULT result = E_FAIL;
    if (GetTokenInformation(token, TokenUser, buffer, sizeof(buffer), &needed)) {
        PWSTR text = nullptr;
        if (ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(buffer)->User.Sid, &text)) {
            result = StringCchCopyW(sid, ARRAYSIZE(sid), text);
            LocalFree(text);
        }
    }
    CloseHandle(token);
    return result;
}
DWORD LockoutSeconds(DWORD attempts) noexcept {
    if (attempts >= 8) return 900;
    if (attempts == 7) return 300;
    if (attempts == 6) return 60;
    return attempts == 5 ? 30 : 0;
}
}
