#include "security.h"
#include "tpm.h"
#include <cstdio>

int main() {
    int failed = 0;
    auto check = [&](bool value, const char* name) {
        if (!value) {
            std::printf("FAIL: %s\n", name);
            ++failed;
        }
    };

    // Pattern hashing and comparison
    patlock::Pattern a; for (BYTE i = 0; i < 5; ++i) a.Add(i);
    patlock::Pattern b; for (int i = 4; i >= 0; --i) b.Add(static_cast<BYTE>(i));
    BYTE salt[16]{};
    patlock::Secret<BYTE, 32> first, second, reversed;
    check(SUCCEEDED(patlock::Random(salt, sizeof(salt))), "Random salt");
    check(SUCCEEDED(patlock::HashPattern(a, salt, first.data())), "HashPattern a");
    check(SUCCEEDED(patlock::HashPattern(a, salt, second.data())), "HashPattern a duplicate");
    check(SUCCEEDED(patlock::HashPattern(b, salt, reversed.data())), "HashPattern b reversed");
    check(patlock::Equal(first.data(), second.data(), 32), "Identical pattern hashes match");
    check(!patlock::Equal(first.data(), reversed.data(), 32), "Reversed pattern hashes differ");

    // Escalating lockout policy (Phase 5)
    // 0-4 attempts: 0s delay
    for (DWORD i = 0; i < 5; ++i) {
        check(patlock::LockoutSeconds(i) == 0, "No delay before 5 attempts");
    }
    // 5 attempts: 30s
    check(patlock::LockoutSeconds(5) == 30, "5 attempts -> 30s delay");
    // 6 attempts: 60s (1 min)
    check(patlock::LockoutSeconds(6) == 60, "6 attempts -> 60s delay");
    // 7 attempts: 300s (5 min)
    check(patlock::LockoutSeconds(7) == 300, "7 attempts -> 300s delay");
    // 8-10 attempts: 900s (15 min)
    check(patlock::LockoutSeconds(8) == 900, "8 attempts -> 900s delay");
    check(patlock::LockoutSeconds(9) == 900, "9 attempts -> 900s delay");
    check(patlock::LockoutSeconds(10) == 900, "10 attempts -> 900s delay");

    // Secret buffer zeroing verification
    {
        patlock::Secret<BYTE, 32> secret;
        for (BYTE& b : secret.value) b = 0xAA;
        secret.Clear();
        bool allZero = true;
        for (BYTE b : secret.value) {
            if (b != 0) allZero = false;
        }
        check(allZero, "Secret::Clear wipes memory");
    }

    // TPM detection (informational, does not fail the test if TPM absent)
    const bool tpmAvail = patlock::tpm::Available();
    std::printf("TPM Platform Crypto Provider available: %s\n", tpmAvail ? "YES" : "NO (baseline fallback active)");

    std::printf("Crypto/policy checks: %s (failures: %d)\n", failed ? "FAIL" : "PASS", failed);
    return failed ? 1 : 0;
}
