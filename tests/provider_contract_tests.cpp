#include "CSampleCredential.h"
#include <cstdio>
#include <new>

// VM ONLY: COM contract checks in an ordinary desktop test process, no install.
int main() {
    auto* credential = new(std::nothrow) CSampleCredential();
    if (!credential) return 1;
    int failures = 0;
    auto check = [&](bool ok, const char* label) { if (!ok) { ++failures; std::printf("FAIL: %s\n", label); } };
    CREDENTIAL_PROVIDER_GET_SERIALIZATION_RESPONSE response{};
    CREDENTIAL_PROVIDER_CREDENTIAL_SERIALIZATION serialized{};
    check(credential->GetSerialization(&response, &serialized, nullptr, nullptr) == S_OK,
        "optional serialization outputs may be null");
    check(response == CPGSR_NO_CREDENTIAL_NOT_FINISHED && serialized.rgbSerialization == nullptr,
        "no pattern produces no credential");
    check(credential->ReportResult(0, 0, nullptr, nullptr) == S_OK, "optional result outputs may be null");
    check(credential->GetSerialization(nullptr, &serialized, nullptr, nullptr) == E_POINTER, "required output validated");
    check(credential->GetFieldState(0, nullptr, nullptr) == E_POINTER, "field state outputs validated");
    check(credential->GetStringValue(0, nullptr) == E_POINTER, "string output validated");
    PWSTR text = nullptr;
    check(credential->GetComboBoxValueAt(SFI_COMBOBOX, 1000, &text) == E_INVALIDARG && !text,
        "combo index bounded");
    check(credential->GetStringValue(SFI_PASSWORD, &text) == S_OK && text && !*text,
        "hidden password getter never exposes pending secret");
    CoTaskMemFree(text);
    check(credential->SetStringValue(SFI_PASSWORD, L"") == E_NOTIMPL, "hidden password cannot be externally replaced");
    credential->Release();
    std::printf("Provider contract checks: %s\n", failures ? "FAIL" : "PASS");
    return failures ? 1 : 0;
}
