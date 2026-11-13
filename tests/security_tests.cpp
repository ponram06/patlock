#include "security.h"
#include <cstdio>
int main() {
    int failed = 0;
    auto check = [&](bool value) { if (!value) ++failed; };
    patlock::Pattern a; for (BYTE i = 0; i < 5; ++i) a.Add(i);
    patlock::Pattern b; for (int i = 4; i >= 0; --i) b.Add(static_cast<BYTE>(i));
    BYTE salt[16]{};
    patlock::Secret<BYTE, 32> first, second, reversed;
    check(SUCCEEDED(patlock::Random(salt, sizeof(salt))));
    check(SUCCEEDED(patlock::HashPattern(a, salt, first.data())));
    check(SUCCEEDED(patlock::HashPattern(a, salt, second.data())));
    check(SUCCEEDED(patlock::HashPattern(b, salt, reversed.data())));
    check(patlock::Equal(first.data(), second.data(), 32));
    check(!patlock::Equal(first.data(), reversed.data(), 32));
    check(patlock::LockoutSeconds(4) == 0);
    check(patlock::LockoutSeconds(5) == 30);
    check(patlock::LockoutSeconds(6) == 60);
    check(patlock::LockoutSeconds(7) == 300);
    check(patlock::LockoutSeconds(8) == 900);
    check(patlock::LockoutSeconds(9) == 900);
    std::printf("Crypto/policy checks: %s\n", failed ? "FAIL" : "PASS");
    return failed ? 1 : 0;
}
