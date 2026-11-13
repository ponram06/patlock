#pragma once
namespace patlock {
// Device-independent geometry shared by rendering, hit testing and VM tests.
struct PatternLayout {
    static constexpr int Width = 360;
    static constexpr int Height = 342;
    static constexpr int Spacing = 90;
    static constexpr int Top = 66;
    static constexpr int HitRadius = 28;
    static constexpr int FallbackTop = 300;
    static constexpr int FallbackHeight = 32;
};
}
