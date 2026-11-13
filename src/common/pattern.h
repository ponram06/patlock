#pragma once
#include <windows.h>
#include <array>
#include <cstddef>
#include <cstdlib>

namespace patlock {
inline constexpr size_t MinimumDots = 5;
struct Pattern final {
    std::array<BYTE, 9> dots{};
    size_t count = 0;
    ~Pattern() { Clear(); }
    void Clear() noexcept { SecureZeroMemory(dots.data(), dots.size()); count = 0; }
    bool Contains(BYTE dot) const noexcept {
        for (size_t i = 0; i < count && i < dots.size(); ++i) if (dots[i] == dot) return true;
        return false;
    }
    void Add(BYTE dot) noexcept {
        if (dot > 8 || count >= dots.size() || Contains(dot)) return;
        if (count) {
            const int previous = dots[count - 1];
            const int dx = std::abs(previous % 3 - dot % 3);
            const int dy = std::abs(previous / 3 - dot / 3);
            if ((dx == 2 && dy == 0) || (dx == 0 && dy == 2) || (dx == 2 && dy == 2)) {
                const BYTE middle = static_cast<BYTE>((previous + dot) / 2);
                if (!Contains(middle)) dots[count++] = middle;
            }
        }
        if (count < dots.size()) dots[count++] = dot;
    }
    bool Valid() const noexcept {
        if (count < MinimumDots || count > dots.size()) return false;
        unsigned seen = 0;
        for (size_t i = 0; i < count; ++i) {
            if (dots[i] > 8 || (seen & (1u << dots[i]))) return false;
            seen |= 1u << dots[i];
        }
        return true;
    }
};
}
