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

using PatternVerifier = std::function<VerifyResult(const Pattern& pattern)>;

// Standard DrawPattern (used by demo or when no verifier provided)
HRESULT DrawPattern(HINSTANCE instance, HWND owner, bool hideTrail, Pattern& output) noexcept;

// Interactive DrawPattern with in-window Android-style instant verification & red flash
HRESULT DrawPatternInteractive(
    HINSTANCE instance,
    HWND owner,
    bool hideTrail,
    const PatternVerifier& verifier,
    Pattern& output) noexcept;
}
