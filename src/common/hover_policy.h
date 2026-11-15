#pragma once
namespace patlock {
// No credentials: timing policy shared by the window and VM regression tests.
struct HoverPolicy {
    // Tuned for casual trackpad use: start quickly, and allow a short hesitation
    // mid-pattern without submitting a partial stroke.
    static constexpr unsigned StartDelayMs = 80;
    static constexpr unsigned FinishDelayMs = 650;
    static constexpr unsigned ShortStrokeDelayMs = 1800;
    static unsigned Remaining(unsigned long long now, unsigned long long deadline) noexcept {
        return deadline > now ? static_cast<unsigned>(deadline - now) : 0;
    }
    static bool CanStart(int pending, int actual, bool armed, bool active, bool drawing) noexcept {
        return drawing && armed && !active && pending >= 0 && pending < 9 && pending == actual;
    }
    static bool CanFinish(bool hover, bool active, bool valid, bool drawing) noexcept {
        return drawing && hover && active && valid;
    }
};
}
