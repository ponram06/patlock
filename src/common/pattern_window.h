#pragma once
#include "pattern.h"
#include <functional>

namespace patlock {
enum class VerifyResult {
    Success,
    WrongPattern,
    Lockout,
    Error
};

// Reports S_OK plus remaining seconds, E_ACCESSDENIED for Password-only fallback.
using RetryDelay = std::function<HRESULT(DWORD* seconds)>;

using PatternVerifier = std::function<VerifyResult(const Pattern& pattern)>;

// Standard DrawPattern (used by demo or when no verifier provided)
HRESULT DrawPattern(HINSTANCE instance, HWND owner, bool hideTrail, Pattern& output) noexcept;

// Deliberate press-drag-release input; verification runs only after release.
// Native fallback remains keyboard accessible. The grid itself is pointer input.
HRESULT DrawPatternInteractive(
    HINSTANCE instance,
    HWND owner,
    bool hideTrail,
    const PatternVerifier& verifier,
    Pattern& output,
    const RetryDelay& retryDelay = nullptr) noexcept;
}
