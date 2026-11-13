#include "hover_policy.h"
#include <cstdio>
int main() {
    using patlock::HoverPolicy;
    int failures = 0;
    auto check = [&](bool ok, const char* name) { if (!ok) { ++failures; std::printf("FAIL: %s\n", name); } };
    check(HoverPolicy::CanStart(4, 4, true, false, true), "stable hover starts without a button");
    check(!HoverPolicy::CanStart(4, 5, true, false, true), "moved pointer rejects stale dwell");
    check(!HoverPolicy::CanStart(4, 4, false, false, true), "opening/reset gate prevents accidental start");
    check(!HoverPolicy::CanStart(4, 4, true, true, true), "active stroke cannot restart");
    check(!HoverPolicy::CanStart(4, 4, true, false, false), "lockout/error cannot start");
    check(!HoverPolicy::CanStart(-1, -1, true, false, true), "outside grid cannot start");
    check(HoverPolicy::CanFinish(true, true, true, true), "valid hover stroke pauses to finish");
    check(!HoverPolicy::CanFinish(false, true, true, true), "held-button stroke waits for release");
    check(!HoverPolicy::CanFinish(true, false, true, true), "cancelled stroke ignores stale finish timer");
    check(!HoverPolicy::CanFinish(true, true, false, true), "short stroke cannot submit");
    check(!HoverPolicy::CanFinish(true, true, true, false), "checking/lockout cannot resubmit");
    check(HoverPolicy::Remaining(100, 220) == 120, "queued old dwell timer waits for new deadline");
    check(HoverPolicy::Remaining(400, 900) == 500, "queued old finish timer waits for new deadline");
    check(HoverPolicy::Remaining(900, 900) == 0, "exact deadline permits handling");
    check(HoverPolicy::Remaining(901, 900) == 0, "late timer has no remaining delay");
    std::printf("Hover policy checks: %s\n", failures ? "FAIL" : "PASS");
    return failures ? 1 : 0;
}
