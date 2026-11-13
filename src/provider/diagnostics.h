#pragma once
#include <windows.h>
#include <strsafe.h>

// Fixed lifecycle message only. No caller text, identity or credential data.
// The VM installer must pre-create the file; failures never affect enumeration.
inline void LogSampleEnumeration() noexcept
{
    wchar_t path[MAX_PATH] = {};
    const UINT length = GetSystemDirectoryW(path, ARRAYSIZE(path));
    if (!length || length >= ARRAYSIZE(path) ||
        FAILED(StringCchCatW(path, ARRAYSIZE(path), L"\\PatlockSample.log"))) return;
    HANDLE file = CreateFileW(path, FILE_APPEND_DATA | FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (file == INVALID_HANDLE_VALUE) return;
    BY_HANDLE_FILE_INFORMATION info = {};
    if (GetFileInformationByHandle(file, &info) &&
        !(info.dwFileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY)) &&
        info.nFileSizeHigh == 0 && info.nFileSizeLow < 1024 * 1024)
    {
        constexpr char message[] = "sample: enumeration requested\r\n";
        DWORD written = 0;
        WriteFile(file, message, static_cast<DWORD>(sizeof(message) - 1), &written, nullptr);
    }
    CloseHandle(file);
}
