# Phase 2 input spike

Use the root CMake project inside the VM, as in PHASE1_VM.md. The current DLL
is now `build/Release/PatlockProvider.dll` and uses the new project CLSID
`{21d42e91-a8c0-4e63-b5d7-372d8585982c}`. Installation scripts now target this
identity. If the Phase 1 sample is installed, use its uninstall script from
commit 0031fb4 in a separate checkout and reboot first.

VM commands:

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release -- /m
ctest --test-dir build -C Release --output-on-failure
.\build\Release\pattern_window_demo.exe
.\install\install.ps1 -DllPath .\build\Release\PatlockProvider.dll -AcknowledgeVm -SnapshotAndPasswordFallbackVerified
```

Verify on the ordinary desktop, VM lock screen and VM sign-in screen:

- Pattern tile, Draw pattern link, hide-trail checkbox; no sample password box.
- Drag/release across at least five dots. Capture reports success without unlock.
- 0 to 2 includes 1; 0 to 8 includes 4; revisiting a dot does not duplicate it.
- Cancel, Escape, lost capture and dialog inactivity leave no usable pattern.
- Fast drags still collect dots crossed between mouse messages.
- Password/PIN still work; unregistering removes only Patlock.
- Missing diagnostic log never blocks the tile or Password sign-in.

The normal demo uses the same window code. Its result never logs the sequence.
Hit radius is one quarter of grid spacing. Compilation, tests, secure-desktop
window ownership and all listed input behavior are UNVERIFIED pending the VM.
Do not claim real-trackpad feel from the virtual mouse.

API reference: https://learn.microsoft.com/en-us/windows/win32/api/credentialprovider/nf-credentialprovider-icredentialprovidercredentialevents-oncreatingwindow
requires parenting every provider dialog to the returned HWND; failure to obtain
it declines the command. The code uses that owner for the modal dialog.
