# Implementation Plan: Trackpad Pattern Unlock for Windows

## 1. Summary
- **Goal:** unlock a Windows laptop by drawing a 3x3 pattern with the trackpad, like Android, on the real sign-in and lock screen.
- **Verdict:** feasible, using an official Windows extension point: a **Credential Provider**.
- **How it works:** we do not bypass Windows. After the pattern matches, our provider hands the stored Windows password to Windows,
  which performs the normal sign-in. Therefore the password must be recoverable by our code, which is the central security risk.
- **Top flaws:** (1) small keyspace, (2) stored password must resist offline theft, (3) a provider bug can break sign-in.
- **Honest framing:** a convenience sign-in with the password as fallback, like Android. Not stronger than a password.

## 2. User experience
1. **Enroll once** from the desktop: enter Windows password, draw the pattern twice, save.
2. **Lock** with Win+L or auto-lock.
3. **Unlock:** a new **Pattern** option appears beside Password and PIN. Select it, a grid window opens, draw the pattern.
4. Correct pattern unlocks. Wrong pattern counts toward lockout. Password option is always available.

## 3. How Windows sign-in works
- The lock and sign-in screen is drawn by **LogonUI.exe** on a protected desktop.
- LogonUI loads every credential provider DLL registered in the registry. Each provider offers tiles.
- On submit, the provider returns a **serialized credential** (username + password packed for Windows). Windows verifies it.
- The provider DLL runs inside LogonUI, so it can create windows on that desktop and receive normal mouse and trackpad input.
- Consequences: we automate password entry after a pattern match; storage security is the main design problem; the provider must
  support both the sign-in and the unlock scenarios.

## 4. Architecture
Components:
1. **Pattern Credential Provider** (C++ x64 DLL): shows the tile, opens the pattern window, verifies, returns the credential.
2. **Pattern window:** custom-drawn 3x3 grid created by the provider (parented to LogonUI via the window-creation callback of the
   provider's events interface). Converts cursor movement into a dot sequence.
3. **Enrollment tool** (elevated, normal desktop): validates the Windows password, records the pattern twice, writes the protected record.
4. **Secure store:** salted pattern hash, encrypted password, attempt counter, lockout time, last full-password time.
5. **Service (optional, later):** only if lockout/secret handling should move out of the DLL.

Unlock data flow:
1. LogonUI loads the provider and shows the Pattern tile.
2. User selects it; the provider opens the pattern window.
3. User draws; window yields a sequence such as 0,1,2,5,8.
4. Provider checks lockout, hashes with the stored salt (PBKDF2), compares in constant time.
5. On match: decrypt password, build the serialized credential, wipe plaintext, return it to LogonUI.
6. On mismatch: increment and persist the counter.

Design note: use normal cursor events inside the provider's own window (works on any trackpad). Raw HID touchpad input is only a
fallback if the spike in section 11 fails, or for a later freehand mode.

## 5. Pattern rules
- Grid 3x3, dots 0-8. Each dot once. Order matters.
- Minimum 5 dots (Android allows 4).
- Skipped-dot rule: passing over an unvisited dot adds it.
- Generous hit radius (about a quarter of the cell width) because trackpad acceleration makes aiming harder.
- Input: press-and-hold or tap-and-drag; consider a hover-style start. Optional hide-trail setting.
- Lockout: 5 wrong -> 30 s; then 1, 5, 15 min; about 10 wrong -> Pattern disabled until full password login or re-enroll. Persisted durably across reboots.
- Restart and freshness policy (Option A, approved 2026-11-13): The volatile boot lease requirement and 24h expiration are dropped. Pattern unlock remains available across reboots (matching Windows Hello PIN behavior). Brute-force guessing is strictly constrained by the durable escalating lockout and 10-attempt cap in locked storage. Password age is configurable via `kMaxPasswordAge` (default 0 / disabled). Stale passwords resulting from password changes are handled by one-shot failure detection and fallback to the built-in Password tile.

## 6. Secure storage
Stored: salt + PBKDF2 hash of the pattern; encrypted password; attempt counter; lockout time; last full-password time; account name; format version.

**Baseline (build first):** random 256-bit key protected by a SYSTEM/Administrators-only store (LSA secret or tightly ACL'd HKLM key);
password encrypted with AES-GCM; wipe buffers; never log secrets; constant-time compare.
Limitation: offline disk access could extract the secret (same weakness as Windows auto-login). **BitLocker/Device Encryption is a prerequisite.**

**Hardened (build second):** TPM-backed key (Platform Crypto Provider) whose use needs an authorization value derived from the pattern,
so the TPM's anti-hammering limits guessing (same idea as Windows Hello PIN). Fall back to baseline without a TPM. Exact API
details need a spike (section 11).

## 7. Flaws and mitigations
1. **Small keyspace** (~389,000 patterns for 4-9 dots; humans pick predictable ones): min 5 dots, durable escalating lockout,
   later maybe 4x4 grid or freehand.
2. **Recoverable stored password:** BitLocker prerequisite, locked-down storage, TPM hardening, document the trade-off.
3. **Provider bug breaks sign-in:** VM + snapshots, second admin account, fail closed, never touch the Password tile, Safe Mode recovery.
4. **Shoulder surfing:** hide-trail option, longer patterns, full-password rule.
5. **Trackpad variability:** large hit targets, tolerate overshoot, test on 2+ laptops, show a start hint.
6. **Password changes:** stored copy goes stale; detect failure, fall back to Password tile, prompt re-enroll; stop auto-submitting after one failure
   to avoid Windows account lockout.
7. **Account types/policies:** target a local account first; test Microsoft accounts separately; document limits.
8. **Unsigned DLL warnings:** code-sign for distribution; keep source open.
9. **Overlap with PIN/Hello/Picture Password:** position honestly as a custom experience and learning project.
10. **Windows updates:** use only documented interfaces; re-test after feature updates.

## 8. Phases (rough part-time estimates)
- Phase 0 (done): Python prototype.
- Phase 1 (~1 wk): VM, Visual Studio, build and register Microsoft's sample provider, see its tile.
- Phase 2 (~1-2 wk): Pattern tile + own grid window; confirm trackpad drag works on lock and sign-in screens.
- Phase 3 (~1 wk): enrollment tool, baseline secure store, hash verification, persistent counter.
- Phase 4 (~1 wk): real unlock via serialized credential; unlock and sign-in scenarios; wipe memory.
- Phase 5 (~1-2 wk): hardening: escalating lockout, full-password rule, TPM key with fallback, fail-closed handling, safe logging.
- Phase 6 (~1 wk): test, installer/uninstaller, README, security notes.
- Phase 7 (a few days): tune on the real trackpad using a standalone pattern-window test app; optional real-hardware install only with the safeguards in `docs/RECOVERY.md`.

## 9. Registration and recovery
- COM class: `HKEY_CLASSES_ROOT\CLSID\{GUID}\InprocServer32` (DLL path, apartment threading).
- Provider: `HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\Windows\CurrentVersion\Authentication\Credential Providers\{GUID}`.
- DLL must be 64-bit on 64-bit Windows.
- Recovery: VM snapshot first; second admin account; Safe Mode and delete the provider key; keep BitLocker recovery key backed up;
  never install untested builds on the main PC. See `docs/RECOVERY.md`.

## 10. Test checklist
See `docs/TESTING.md`.

## 11. Open questions to verify early (spike)
1. Does the pattern window created via the provider's window-creation callback receive trackpad drag input reliably on both the lock
   and sign-in screens of current Windows 11?
2. Exact TPM key-policy approach for a pattern-derived authorization value, and behavior if the TPM is reset.
3. Behavior with Microsoft accounts and with the policy requiring Windows Hello for them.
4. Whether security software flags the unsigned DLL during development.
If (1) fails: fall back to raw touchpad input in a helper process (more complex, still possible).

## 12. Recommendation
Build it, in a VM, starting with Phase 1. Treat the pattern as quick-unlock convenience, make BitLocker a stated prerequisite,
and strictly enforce durable lockouts.

## 13. Development environment and workflow
- Two machines: main PC (Codex, git) and a Windows 11 Pro VM in VMware Workstation Pro (build, install, test). Full steps in `docs/SETUP_GUIDE.md`.
- Windows ISO: the regular free Windows 11 ISO (install Pro, no product key) is preferred over the 90-day Enterprise evaluation. Microsoft's old prebuilt developer VM was an expired 2024 image and is not usable.
- VM: 8 GB RAM, 4 cores, 80 GB disk, UEFI + Secure Boot + virtual TPM (encrypt only TPM-supporting files; keep the encryption password safe).
- Snapshots: `clean`, `tools-ready`, then one before every install and `phaseN-pass` after each passing phase.
- Code moves by git (private repo): `push` on the main PC, `pull` in the VM.
- Codex: newest flagship model from the picker; medium for implementation, high for planning/debugging/security, xhigh rarely; fresh session per phase.

## 14. Limits of the VM
VMware gives the guest a virtual mouse. It proves the pattern window receives drag input on the lock screen but not how it feels on the real trackpad (acceleration, tap-and-drag, palm rejection). Phase 7 covers this with a standalone test app first, and an optional guarded real-hardware install second.
