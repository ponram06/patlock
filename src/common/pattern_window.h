#pragma once
#include "pattern.h"
namespace patlock {
// S_OK: completed pattern; S_FALSE: cancelled; failure: no usable pattern.
HRESULT DrawPattern(HINSTANCE instance, HWND owner, bool hideTrail, Pattern& output) noexcept;
}
