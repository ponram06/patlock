#pragma once
namespace patlock {
// Device-independent geometry shared by rendering, hit testing and VM tests.
// The normal layout leaves LogonUI's account header untouched: native username,
// pattern grid, then the fallback action.
struct PatternLayout {
    // Compact PIN-box-like block: grid + one status line. No in-window fallback
    // button: LogonUI's own "Sign-in options" row sits directly below this block.
    static constexpr int Width          = 300;
    static constexpr int Height         = 232;
    static constexpr int Spacing        = 72;
    static constexpr int Top            = 36;
    // The visual dot is smaller; this radius is the usable pointer target.
    // Must stay below Spacing / 2 so neighbouring targets never overlap.
    static constexpr int HitRadius      = 30;
    static constexpr int StatusTop      = 200;
    static constexpr int StatusHeight   = 24;
    // Gap between the native username text and the top of this block.
    static constexpr int AnchorGap      = 10;
    // Used only when UI Automation cannot locate the native username.
    static constexpr int FallbackCenterOffset = 35;
};
}
