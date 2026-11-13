#pragma once
// Decides when a NEW pattern stroke may be started by hovering.
//
// Problem it solves: if the pointer is already resting on a dot when the grid opens (or after Esc,
// a wrong pattern, a too-short stroke...), a plain "hover on a dot starts a stroke" rule starts a stroke
// the user never intended.
//
// Rule: after every reset the window calls Disarm() with the pointer position.
//   * If the pointer is NOT on a dot at that moment, the gate is armed immediately.
//   * If it IS on a dot, the gate stays disarmed until the pointer has travelled at least
//     kTravelPx pixels away from where it was. Tiny jitter on the dot never arms it.
// Pure logic, no Windows dependency, so it can be unit-tested anywhere.
namespace patlock {

class ArmingGate final {
public:
    static constexpr int kTravelPx = 40;

    void Disarm(int x, int y, bool pointerIsOnDot) noexcept {
        anchorX_ = x;
        anchorY_ = y;
        armed_ = !pointerIsOnDot;
    }

    // Call for every pointer move. Returns true when a stroke may start.
    bool Observe(int x, int y) noexcept {
        if (!armed_) {
            const long long dx = static_cast<long long>(x) - anchorX_;
            const long long dy = static_cast<long long>(y) - anchorY_;
            if (dx * dx + dy * dy >= static_cast<long long>(kTravelPx) * kTravelPx) armed_ = true;
        }
        return armed_;
    }

    bool Armed() const noexcept { return armed_; }

private:
    bool armed_ = false;  // closed until the first Disarm()/Observe() says otherwise
    int anchorX_ = 0;
    int anchorY_ = 0;
};

}  // namespace patlock
