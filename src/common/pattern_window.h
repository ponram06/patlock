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

// Hover to start, move without holding a button, pause to submit a valid stroke.
// Press-drag-release remains available as an alternative.
// Native fallback remains keyboard accessible. The grid itself is pointer input.
// Modeless entry point for the lock screen. Returns S_OK once the window exists;
// onDone is then called exactly once (S_OK with the pattern, S_FALSE on cancel,
// or a failure). It never runs a message loop, so LogonUI stays responsive.
using PatternDone = std::function<void(HRESULT hr, const Pattern& pattern)>;
HRESULT ShowPatternWindow(
    HINSTANCE instance,
    HWND owner,
    bool hideTrail,
    PatternVerifier verifier,
    RetryDelay retryDelay,
    const wchar_t* anchorName,
    PatternDone onDone) noexcept;
// Closes the open pattern window (if any) without authenticating. Call when
// LogonUI deselects the tile, e.g. the user picked another sign-in option.
void CloseActivePatternWindow() noexcept;

HRESULT DrawPatternInteractive(
    HINSTANCE instance,
    HWND owner,
    bool hideTrail,
    const PatternVerifier& verifier,
    Pattern& output,
    const RetryDelay& retryDelay = nullptr,
    const wchar_t* anchorName = nullptr) noexcept;
}
