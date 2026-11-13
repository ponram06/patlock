#pragma once
#include "pattern.h"
#include <array>

namespace patlock {
template<class T, size_t N> struct Secret final {
    std::array<T, N> value{};
    Secret() = default;
    Secret(const Secret&) = delete;
    Secret& operator=(const Secret&) = delete;
    ~Secret() { Clear(); }
    void Clear() noexcept { SecureZeroMemory(value.data(), sizeof(value)); }
    T* data() noexcept { return value.data(); }
};
using Password = Secret<wchar_t, 513>;
inline constexpr ULONG HashIterations = 600000;
bool Equal(const BYTE* left, const BYTE* right, size_t length) noexcept;
HRESULT Random(BYTE* output, ULONG length) noexcept;
HRESULT HashPattern(const Pattern& pattern, const BYTE salt[16], BYTE hash[32]) noexcept;
// Account must be an unqualified local username; SID comes from the verified token.
HRESULT ValidatePassword(PCWSTR user, PCWSTR password, wchar_t (&sid)[184]) noexcept;
HRESULT Enroll(PCWSTR user, PCWSTR sid, PCWSTR password, const Pattern& pattern) noexcept;
HRESULT Refresh(PCWSTR user, PCWSTR sid, PCWSTR password) noexcept;
HRESULT Verify(PCWSTR sid, const Pattern& pattern, Password* password = nullptr) noexcept;
HRESULT Disable(PCWSTR sid) noexcept;
HRESULT Eligible(PCWSTR sid) noexcept;
// Pure policy helpers, also used by VM tests. Seconds and failure counts only.
DWORD LockoutSeconds(DWORD attempts) noexcept;
}
