# Phase 1 sample-provider spike (not a pattern implementation)

No build or installation has been performed by the agent. Use a Windows 11 Pro
VMware VM with Visual Studio 2022 Desktop development with C++, Windows SDK and
CMake installed. Take a VM snapshot and verify the second administrator's
Password login before following these steps. Never run them on the host.

From the repository root in the VM's Developer PowerShell:

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release -- /m
```

Inspect all compiler warnings. A successful build has not yet been established.
The root CMake project selects /MT for Release and /MTd for Debug; it uses the
sample's COM class factory and packing helpers. The old upstream .sln is kept
as reference and targets a legacy toolset; do not use it for this build.

In elevated 64-bit PowerShell **inside the VM**:

```powershell
.\install\install.ps1 -DllPath .\build\Release\SampleV2CredentialProvider.dll -AcknowledgeVm -SnapshotAndPasswordFallbackVerified
```

The scripts recognize VMware, require explicit acknowledgement and refuse to
overwrite an existing sample installation. This is an accident guard, not a
security boundary. The raw .reg files have no VM guard and are only for manual
VM recovery/reference. Never import them on the host.

## Acceptance checks for the human

1. Press Win+L. Check Sign-in options for the sample tile. If necessary reboot
   the VM. Record whether it appears, along with the Windows build number.
2. Use the built-in Password tile and verify normal sign-in. Check PIN if configured.
3. The sample is associated with the first enumerated user only. Test a
   throwaway local account; multi-user behavior is not implemented here.
4. Remove the sample in elevated VM PowerShell:

   ```powershell
   .\install\uninstall.ps1 -AcknowledgeVm
   ```

5. Reboot the VM. Verify the sample disappears and Password still works.
6. Only after the reboot, remove the old sample DLL from the VM's System32
   directory before a subsequent installation. Do not replace a loaded DLL.
7. Record build output, warnings and each result in PROGRESS.md. Do not share
   passwords, pattern sequences or credential dumps.

The installer creates `%SystemRoot%\System32\PatlockSample.log`. The provider
appends only the fixed text `sample: enumeration requested`, stops at about
1 MiB and skips logging if the file cannot be opened. No credential data or
user-controlled text is passed to the logger. Test missing and read-only log
files in the VM; logging failure must not prevent Password sign-in.

If sign-in fails, revert the snapshot. The fixed-CLSID unregister.reg is also
available for manual Safe Mode recovery in the VM; WMI-dependent script checks
may not run in Safe Mode. See RECOVERY.md.

This spike does not implement enrollment, pattern recognition, lockout, TPM,
password freshness, or the standalone pattern demo. Do not treat it as the
finished product or distribute its unsigned DLL.
