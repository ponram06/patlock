#pragma once
namespace patlock {
// Device-independent geometry shared by rendering, hit testing and VM tests.
// The normal layout leaves LogonUI's account header untouched: native username,
// pattern grid, then the fallback action.
struct PatternLayout {
    static constexpr int Width          = 360;
    static constexpr int Height         = 342;
    static constexpr int Spacing        = 90;
    static constexpr int Top            = 66;
    // The visual dot is smaller; this radius is the usable pointer target.
    static constexpr int HitRadius      = 34;
    static constexpr int StatusTop      = 264;
    static constexpr int StatusHeight   = 24;
    static constexpr int FallbackTop    = 300;
    static constexpr int FallbackHeight = 40;
};
}
