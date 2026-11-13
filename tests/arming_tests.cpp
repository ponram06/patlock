#include "arming_gate.h"
#include <cstdio>

int main() {
    int failures = 0;
    auto check = [&](bool ok, const char* what) {
        if (!ok) { ++failures; std::printf("FAIL: %s\n", what); }
    };
    using patlock::ArmingGate;

    // Opening with the pointer on a dot: nothing may start, even with jitter.
    ArmingGate g;
    g.Disarm(100, 100, true);
    check(!g.Armed(), "disarmed when pointer is on a dot");
    check(!g.Observe(100, 100), "no movement stays disarmed");
    check(!g.Observe(105, 103), "small jitter stays disarmed");
    check(!g.Observe(100 + ArmingGate::kTravelPx - 1, 100), "just under the travel distance stays disarmed");
    check(!g.Observe(100, 100 + 20), "moving away and back does not accumulate");
    check(g.Observe(100 + ArmingGate::kTravelPx, 100), "deliberate travel arms the gate");
    check(g.Observe(100, 100), "once armed it stays armed until the next reset");

    // Esc pressed with the pointer on a dot: same rule again.
    g.Disarm(50, 60, true);
    check(!g.Armed(), "reset disarms again");
    check(!g.Observe(52, 61), "jitter after reset does not arm");
    check(g.Observe(50, 60 + 41), "travel after reset arms");

    // Pointer not on a dot at reset time: armed immediately.
    g.Disarm(10, 10, false);
    check(g.Armed(), "armed immediately when not on a dot");

    // Fresh gate (before any Disarm) is closed and needs travel from (0,0).
    ArmingGate fresh;
    check(!fresh.Armed(), "fresh gate is closed");

    std::printf("Arming gate checks: %s\n", failures ? "FAIL" : "PASS");
    return failures ? 1 : 0;
}
