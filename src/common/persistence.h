#pragma once
#include <windows.h>
namespace patlock {
// Replace one value using a KTM registry transaction, on an ordinary open key.
HRESULT WriteRegistryBlob(HKEY key, PCWSTR name, const void* input, DWORD length) noexcept;
HRESULT DeleteRegistryValue(HKEY key, PCWSTR name) noexcept;
}
