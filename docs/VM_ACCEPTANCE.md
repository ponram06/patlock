# VM acceptance: current release candidate

No source inspection substitutes for these checks. Current record format is 6;
re-enroll after upgrading from older records. Use only a throwaway local account.
Keep the working Password provider, a second administrator and a VM snapshot.
Do not run provider builds, enrollment or tests on the host PC.

## Build and package

Inside elevated Developer PowerShell in the Windows 11 VMware VM:

```powershell
git pull --ff-only
.\install\build-and-test.ps1 -AcknowledgeVm
```

Inspect warnings as well as exit codes. This builds C++17 x64 with /MT and runs
pattern rules, input arming, crypto/policy and registry transaction tests.
The registry test uses its own temporary HKCU key and removes it.
If any step fails, stop before installation and report the error without secrets.

## Upgrade and recovery

1. Take a snapshot and verify Password login for the second administrator.
2. Unregister the old provider with the guarded uninstall script, reboot and
   verify the Pattern tile is absent while Password still works.
3. Remove only the old PatlockProvider.dll from System32 in the VM after reboot.
   Installer deliberately refuses to overwrite a loaded/existing DLL.
4. Install the new DLL:

   ```powershell
   .\install\install.ps1 -DllPath .\build\Release\PatlockProvider.dll -AcknowledgeVm -SnapshotAndPasswordFallbackVerified
   .\build\Release\patlock_enroll.exe
   ```

5. Enter a throwaway local account's actual Windows password and draw the same
   pattern twice. Check the reported storage mode. A TPM operational failure must
   abort enrollment; only genuinely missing TPM permits baseline mode.
6. If sign-in fails, use Password or revert the snapshot. Never disable Password.

## Authentication and failure cases

- [ ] Lock/unlock successfully at least ten times; no success leaves a pending guard.
- [ ] Sign out/in and Restart: Option A permits pattern across reboots.
- [ ] Wrong/reversed patterns never unlock. Five wrong attempts cause 30 seconds;
      subsequent delays are 1, 5 and 15 minutes; ten require password refresh.
- [ ] Lockout countdown comes from saved state and survives reopening/reboot.
- [ ] Refresh with the full Windows password resets counters; PIN is not accepted.
- [ ] Change Windows password, submit the stored credential once, then reopen:
      further stored-password attempts must decline until re-enrollment.
- [ ] Interrupt LogonUI after reservation and before submission/result callback:
      Pattern stays disabled. Password and re-enrollment still recover.
- [ ] Nullable optional outputs for GetSerialization/ReportResult do not skip
      serialization/result processing; wrong completion tokens cannot change state.
- [ ] Non-admin cannot read the record. Corrupt/missing record, changed MAC/tag,
      clock rollback and abandoned/timed-out mutex all decline Pattern.
- [ ] Inject failure into transaction creation, write and commit: the old record
      remains unchanged; no failed operation clears lockout or enables credentials.
- [ ] Interrupt re-enrollment at each key-generation/encryption/commit boundary:
      old TPM key is retained until commit; newly created failed generations are
      removed; postcommit retirement failure reports cleanup warning, not lost enrollment.
- [ ] Remove enrollment with `patlock_enroll.exe --remove`: correct password and
      confirmation required; record/current key removed; Password still works.
- [ ] Test fast user switching and selecting/deselecting while the modal is open;
      timers cannot submit for a deselected or destroyed credential.

## UI and real-trackpad acceptance

- [ ] Hover briefly to start, draw without clicking and pause 500 ms to submit a
      valid pattern. Opening/reset on a dot never starts accidentally; moving off
      the candidate before its dwell finishes prevents start. Drag/release also works.
- [ ] Capture loss, Escape, fallback and inactivity discard abandoned strokes.
- [ ] Fallback has keyboard focus and returns to the native sign-in options.
- [ ] Narrator reads status/fallback; pointer-only grid has Password as keyboard alternative.
- [ ] High contrast and 100/125/150/200% display scales are legible and usable.
- [ ] Mixed-DPI monitor movement and small work areas do not clip controls.
- [ ] Correct-pattern text does not claim Windows authentication has succeeded.
- [ ] Shared standalone demo tested on real trackpads before real provider install.

Run TPM_VALIDATION.md separately; a successful wrapping roundtrip is not proof
of wrong-authorization rejection or hardware anti-hammering.

## Distribution gate

All applicable checks must have human-recorded evidence, with Windows build,
hardware/storage mode, commit and results in PROGRESS.md. Sign distributed DLL
and enrollment/demo executables with the publisher's certificate and verify their
Authenticode signatures in the VM. No signing certificate is bundled.
Package only after those checks:

```powershell
.\install\package.ps1 -OutputDir dist\Patlock-candidate
```

The packaging script refuses to recursively replace an existing output directory
and emits SHA256SUMS.txt. Review the archive and signatures before distribution.
No automated claim of production readiness is made by that script.
