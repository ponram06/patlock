#include "pattern.h"
#include <cstdio>
int main() {
    int failures = 0;
    auto check = [&](bool condition) { if (!condition) ++failures; };
    patlock::Pattern p;
    p.Add(0); p.Add(8); check(p.count == 3 && p.dots[1] == 4);
    p.Add(0); check(p.count == 3);
    p.Clear(); p.Add(0); p.Add(2); check(p.count == 3 && p.dots[1] == 1);
    p.Clear(); p.Add(0); p.Add(6); check(p.count == 3 && p.dots[1] == 3);
    p.Clear(); p.Add(1); p.Add(6); check(p.count == 2);
    p.Clear(); p.Add(4); p.Add(0); p.Add(8); check(p.count == 3);
    p.Clear(); for (BYTE i = 0; i < 4; ++i) p.Add(i); check(!p.Valid());
    p.Add(4); check(p.Valid()); p.Add(255); check(p.count == 5);
    p.dots[0] = 255; check(!p.Valid());
    p.Clear(); for (BYTE i = 0; i < 9; ++i) p.Add(i); check(p.count == 9);
    p.Clear(); check(p.count == 0); for (BYTE value : p.dots) check(value == 0);
    std::printf("Pattern checks: %s\n", failures ? "FAIL" : "PASS");
    return failures ? 1 : 0;
}
