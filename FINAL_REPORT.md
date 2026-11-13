# FINAL REPORT: Trackpad Pattern Unlock for Windows

*Complete master document. Prepared 13 November 2026.*

This single file contains everything: environment setup, workflow, architecture, security, phases, tests, recovery and the Codex prompts.

## Contents
- Part A: Setup guide (VMware VM, tools, git, Codex)
- Part B: Implementation plan
- Part C: Phases 1 to 7
- Part D: Security notes
- Part E: Test checklist
- Part F: Recovery plan
- Part G: Codex rules (AGENTS.md)
- Part H: Prompts
- Part I: Progress log template


---
# Part A: Setup guide
## Complete Setup Guide: from zero to Phase 1

### 0. The big picture
You use TWO environments and never mix them up.

| | Main PC (your real laptop) | Windows 11 VM (VMware Workstation Pro) |
| --- | --- | --- |
| Runs | Codex, git, your editor | Visual Studio, build, install, lock-screen tests |
| Does | Writes and commits code, pushes to GitHub | Pulls code, builds, registers the provider, locks, tests |
| Never does | Install or register the provider (until Phase 7) | Hold your real password or real data |

Flow: `Codex (main PC) -> git push -> GitHub (private) -> git pull (VM) -> build -> test -> paste results back to Codex`.

### 1. Pre-flight checklist (finish this before Phase 1)
- [ ] Virtualization is on: Task Manager > Performance > CPU shows "Virtualization: Enabled" (if not, enable it in BIOS/UEFI).
- [ ] At least 120 GB free disk (ISO 6-8 GB, VM disk up to 80 GB, snapshots grow too).
- [ ] RAM: give the VM 8 GB; close extra browser tabs while it runs.
- [ ] VMware Workstation Pro installed.
- [ ] GitHub account, and a private repository created (empty).
- [ ] Codex access with your ChatGPT Pro account.
- [ ] You have read `docs/RECOVERY.md`.
- [ ] If the VM is very slow or VMware complains about Hyper-V or Core isolation, search "VMware Workstation Hyper-V compatibility"; it is a known setup issue, not a project problem.

### 2. Get the Windows 11 ISO
Recommended: the regular free Windows 11 ISO from Microsoft's Windows 11 download page ("Download Windows 11 Disk Image (ISO) for x64 devices", the multi-edition ISO).
- During setup choose "I don't have a product key" and pick **Windows 11 Pro**.
- It runs unactivated with no time limit (watermark and locked personalization only), which suits a test VM.

Alternative: the Windows 11 Enterprise **evaluation** ISO from Microsoft's Evaluation Center. It works for 90 days, then nags and shuts down hourly.
Do NOT use Microsoft's old prebuilt "development environment" VM: the downloadable image was an expired 2024 evaluation build.

### 3. Create the VM in VMware Workstation Pro
1. File > New Virtual Machine > **Typical** > select the Windows 11 ISO.
2. If Easy Install appears: enter a name and a **throwaway password** (this makes a local account; no Microsoft account needed). Choose Windows 11 Pro if asked.
3. Guest OS: Microsoft Windows > Windows 11 x64.
4. Encryption: Windows 11 needs a virtual TPM, so VMware asks you to encrypt the VM. Choose to encrypt **only the files needed to support the TPM**.
   **Write the encryption password down somewhere safe. If you lose it, the VM cannot be opened.**
5. Disk: 80 GB, do not pre-allocate.
6. Customize Hardware: Memory 8192 MB; Processors 1 processor with 4 cores; Network NAT.
7. Options > Advanced: firmware UEFI with Secure Boot on (normally the default for Windows 11).

### 4. Install Windows and take the first snapshot
1. Power on; if it says "press any key to boot from DVD", press a key immediately.
2. Install Windows 11 Pro; create a local account (the throwaway one).
3. Install **VMware Tools** (VM menu) for screen size, clipboard and smooth mouse, then reboot.
4. Create a second local administrator account named `admin2` with its own throwaway password (recovery account).
5. Snapshot: VM > Snapshot > Take Snapshot, name it **`clean`**.

### 5. Install the dev tools inside the VM
1. **Visual Studio 2022 Community.** In the installer select:
   - **Desktop development with C++** (this includes MSVC and the Windows 11 SDK; confirm the SDK box is ticked).
   - Optional: **.NET desktop development** if you will write the enrollment tool in C#.
   - Optional: the Windows SDK Debugging Tools (WinDbg).
2. **Git for Windows** (includes Git Credential Manager for GitHub login).
3. Settings > Windows Update > pause updates for a few weeks, so the VM does not restart mid-test.
4. Settings > Power: set screen/sleep to never (the lock test is manual).
5. Snapshot: **`tools-ready`**.

### 6. Git and GitHub
Main PC (once), inside the unzipped project folder:
```
git init
git add .
git commit -m "initial plan"
git branch -M main
git remote add origin <your private repo URL>
git push -u origin main
```
VM (once): `git clone <your private repo URL>` (sign in through the browser prompt). After that, `git pull` before every build.
Rules: keep the repo private; never commit passwords, keys, or the VM credentials. The included `.gitignore` excludes build output and logs.

### 7. Codex on the main PC
- Open the project folder in Codex. It reads `AGENTS.md` automatically.
- **Model:** pick the newest flagship coding model your picker shows. Documentation I found lists a GPT-5.6 family (`gpt-5.6-sol` frontier, `gpt-5.6-terra` balanced, `gpt-5.6-luna` fast) and older `gpt-5.5` / `gpt-5.4`; names change often and this was not confirmed on OpenAI's own pages, so trust your picker. Use the top flagship. Avoid "mini" and "spark" models for this project.
- **Reasoning effort:**
  - medium: most work (windows, tools, scripts, installer)
  - high: planning each phase, debugging crashes, credential serialization, storage/TPM code, security review
  - xhigh: rare, only for a stubborn bug that high could not solve (uses several times more of your usage limit)
- Optional `~/.codex/config.toml` (check against your Codex version):
```
model_reasoning_effort = "medium"
plan_mode_reasoning_effort = "high"
```
- Start a fresh Codex session for each phase. `AGENTS.md`, `PLAN.md` and `docs/PROGRESS.md` carry the context between sessions.
- Paste prompts from `prompts/START_PROMPT.md` and `prompts/PHASE_PROMPTS.md`.

### 8. The loop for every phase
1. **Main PC:** Codex plans (high), you approve, Codex implements (medium) and commits.
2. **Main PC:** `git push`.
3. **VM:** take a snapshot (name it `phaseN-attemptM`), then `git pull`.
4. **VM:** build in Visual Studio (x64, Debug first), run the register script as administrator, lock, test.
5. **VM:** collect evidence (screenshot, Event Viewer errors, the provider's log file).
6. **Main PC:** paste the evidence into Codex. Codex fixes. Repeat.
7. When the phase's "Done when" checks pass: update `docs/PROGRESS.md`, commit, and take a snapshot named `phaseN-pass`.

### 9. Testing inside VMware
- **Lock the VM:** Start menu > account icon > Lock (Win+L may be caught by the host). **Ctrl+Alt+Del:** VM menu > Send Ctrl+Alt+Del.
- **Sign-in screen test:** restart the VM and check the tile there too.
- **Logs:** Event Viewer (`eventvwr.msc`) > Windows Logs > Application; look for LogonUI and Application Error entries. Also check the provider's own log file (location chosen by the code, documented in `docs/PROGRESS.md`).
- **Deployment of the DLL:** follow the official sample's instructions (the DLL is typically copied to the system folder and registered with a .reg file). Use the register/unregister scripts Codex produces.
- **Link the C/C++ runtime statically** (the /MT option) so the DLL does not need extra runtime files inside LogonUI. Ask Codex to confirm this choice in Phase 1.
- **Important limitation:** the VM gives your pattern window a *virtual mouse*, not your real trackpad. The VM proves that dragging works on the lock screen; it does NOT prove how it feels with your trackpad's acceleration and tap-and-drag. See Phase 7.

### 10. Recovery steps in the VM (when sign-in breaks)
1. **Fastest:** VM > Snapshot > revert to the last good snapshot.
2. **Safe Mode:** on the sign-in screen hold Shift and click Power > Restart, then Troubleshoot > Advanced options > Startup Settings > Restart > press 4 (Safe Mode). Run the unregister script or delete the provider key under `HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\Authentication\Credential Providers\{our GUID}`.
3. **Second admin account** `admin2`: sign in with it and run the uninstall script.
4. **Recovery Environment:** from Advanced options open Command Prompt and remove the key from the offline registry.

### 11. Troubleshooting table
| Symptom | Likely cause | What to try |
| --- | --- | --- |
| Tile never appears | DLL is 32-bit, wrong path, GUID mismatch between the two registry keys, missing dependency | Build x64; compare both registry entries; link runtime statically; check Event Viewer |
| Tile appears but selecting it crashes LogonUI | Unhandled exception in the DLL | Read the log and Application error; wrap entry points in try/catch; revert snapshot |
| Pattern window does not appear | Window created with wrong parent or on wrong thread | Check the provider's window-creation callback usage against the official sample |
| Window appears but ignores drag | Input not reaching it, or the window never gets focus | Log mouse messages (debug build, no secrets); report to Codex; see fallback in PLAN section 11 |
| Correct pattern shows success but does not unlock | Credential serialization or authentication package problem | Compare against the sample's packing code; check Event Viewer Security log for the failure reason |
| Sign-in fails after password change | Stored password is stale | Re-enroll; the provider must fall back to Password tile |
| VM very slow | RAM pressure or Hyper-V/VBS conflict | Close host tabs; check VMware's Hyper-V compatibility notes |
| VM will not open | Lost the TPM encryption password | No recovery; recreate the VM. Keep the password safe |
| Windows Update restarted the VM | Updates not paused | Pause updates; re-run the register step if needed |

### 12. Phase 7 preview: real hardware
After every VM test passes, tune the trackpad feel safely by running the pattern window as a **standalone desktop test app on your main PC** (no provider install). Only consider installing the provider on a real machine after full testing and with the safeguards in `docs/RECOVERY.md`.

### 13. Time and effort (rough, part-time)
Setup (this guide): 1 day. Phases 1-6: 6 to 9 weeks. Phase 7: a few days.

### 14. Honest limitations
- Nothing in this package has been run on a real Windows lock screen yet; the first real proof is Phase 1 and Phase 2 in your VM.
- The password-recovery design is a convenience trade-off, not stronger than a password.
- Some Windows API details must be confirmed against Microsoft's official documentation and sample during the build.

---
# Part B: Implementation plan
## Implementation Plan: Trackpad Pattern Unlock for Windows

### 1. Summary
- **Goal:** unlock a Windows laptop by drawing a 3x3 pattern with the trackpad, like Android, on the real sign-in and lock screen.
- **Verdict:** feasible, using an official Windows extension point: a **Credential Provider**.
- **How it works:** we do not bypass Windows. After the pattern matches, our provider hands the stored Windows password to Windows,
  which performs the normal sign-in. Therefore the password must be recoverable by our code, which is the central security risk.
- **Top flaws:** (1) small keyspace, (2) stored password must resist offline theft, (3) a provider bug can break sign-in.
- **Honest framing:** a convenience sign-in with the password as fallback, like Android. Not stronger than a password.

### 2. User experience
1. **Enroll once** from the desktop: enter Windows password, draw the pattern twice, save.
2. **Lock** with Win+L or auto-lock.
3. **Unlock:** a new **Pattern** option appears beside Password and PIN. Select it, a grid window opens, draw the pattern.
4. Correct pattern unlocks. Wrong pattern counts toward lockout. Password option is always available.

### 3. How Windows sign-in works
- The lock and sign-in screen is drawn by **LogonUI.exe** on a protected desktop.
- LogonUI loads every credential provider DLL registered in the registry. Each provider offers tiles.
- On submit, the provider returns a **serialized credential** (username + password packed for Windows). Windows verifies it.
- The provider DLL runs inside LogonUI, so it can create windows on that desktop and receive normal mouse and trackpad input.
- Consequences: we automate password entry after a pattern match; storage security is the main design problem; the provider must
  support both the sign-in and the unlock scenarios.

### 4. Architecture
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

### 5. Pattern rules
- Grid 3x3, dots 0-8. Each dot once. Order matters.
- Minimum 5 dots (Android allows 4).
- Skipped-dot rule: passing over an unvisited dot adds it.
- Generous hit radius (about a quarter of the cell width) because trackpad acceleration makes aiming harder.
- Input: press-and-hold or tap-and-drag; consider a hover-style start. Optional hide-trail setting.
- Lockout: 5 wrong -> 30 s; then 1, 5, 15 min; about 10 wrong -> Pattern disabled until full password login. Persisted across reboots.
- Full password required after every restart and after 24 h without one.

### 6. Secure storage
Stored: salt + PBKDF2 hash of the pattern; encrypted password; attempt counter; lockout time; last full-password time; account name; format version.

**Baseline (build first):** random 256-bit key protected by a SYSTEM/Administrators-only store (LSA secret or tightly ACL'd HKLM key);
password encrypted with AES-GCM; wipe buffers; never log secrets; constant-time compare.
Limitation: offline disk access could extract the secret (same weakness as Windows auto-login). **BitLocker/Device Encryption is a prerequisite.**

**Hardened (build second):** TPM-backed key (Platform Crypto Provider) whose use needs an authorization value derived from the pattern,
so the TPM's anti-hammering limits guessing (same idea as Windows Hello PIN). Fall back to baseline without a TPM. Exact API
details need a spike (section 11).

### 7. Flaws and mitigations
1. **Small keyspace** (~389,000 patterns for 4-9 dots; humans pick predictable ones): min 5 dots, lockout, full password after restart,
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

### 8. Phases (rough part-time estimates)
- Phase 0 (done): Python prototype.
- Phase 1 (~1 wk): VM, Visual Studio, build and register Microsoft's sample provider, see its tile.
- Phase 2 (~1-2 wk): Pattern tile + own grid window; confirm trackpad drag works on lock and sign-in screens.
- Phase 3 (~1 wk): enrollment tool, baseline secure store, hash verification, persistent counter.
- Phase 4 (~1 wk): real unlock via serialized credential; unlock and sign-in scenarios; wipe memory.
- Phase 5 (~1-2 wk): hardening: escalating lockout, full-password rule, TPM key with fallback, fail-closed handling, safe logging.
- Phase 6 (~1 wk): test, installer/uninstaller, README, security notes.
- Phase 7 (a few days): tune on the real trackpad using a standalone pattern-window test app; optional real-hardware install only with the safeguards in `docs/RECOVERY.md`.

### 9. Registration and recovery
- COM class: `HKEY_CLASSES_ROOT\CLSID\{GUID}\InprocServer32` (DLL path, apartment threading).
- Provider: `HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\Windows\CurrentVersion\Authentication\Credential Providers\{GUID}`.
- DLL must be 64-bit on 64-bit Windows.
- Recovery: VM snapshot first; second admin account; Safe Mode and delete the provider key; keep BitLocker recovery key backed up;
  never install untested builds on the main PC. See `docs/RECOVERY.md`.

### 10. Test checklist
See `docs/TESTING.md`.

### 11. Open questions to verify early (spike)
1. Does the pattern window created via the provider's window-creation callback receive trackpad drag input reliably on both the lock
   and sign-in screens of current Windows 11?
2. Exact TPM key-policy approach for a pattern-derived authorization value, and behavior if the TPM is reset.
3. Behavior with Microsoft accounts and with the policy requiring Windows Hello for them.
4. Whether security software flags the unsigned DLL during development.
If (1) fails: fall back to raw touchpad input in a helper process (more complex, still possible).

### 12. Recommendation
Build it, in a VM, starting with Phase 1. Treat the pattern as quick-unlock convenience, make BitLocker a stated prerequisite,
require the full password after restarts, enforce lockouts.

### 13. Development environment and workflow
- Two machines: main PC (Codex, git) and a Windows 11 Pro VM in VMware Workstation Pro (build, install, test). Full steps in `docs/SETUP_GUIDE.md`.
- Windows ISO: the regular free Windows 11 ISO (install Pro, no product key) is preferred over the 90-day Enterprise evaluation. Microsoft's old prebuilt developer VM was an expired 2024 image and is not usable.
- VM: 8 GB RAM, 4 cores, 80 GB disk, UEFI + Secure Boot + virtual TPM (encrypt only TPM-supporting files; keep the encryption password safe).
- Snapshots: `clean`, `tools-ready`, then one before every install and `phaseN-pass` after each passing phase.
- Code moves by git (private repo): `push` on the main PC, `pull` in the VM.
- Codex: newest flagship model from the picker; medium for implementation, high for planning/debugging/security, xhigh rarely; fresh session per phase.

### 14. Limits of the VM
VMware gives the guest a virtual mouse. It proves the pattern window receives drag input on the lock screen but not how it feels on the real trackpad (acceleration, tap-and-drag, palm rejection). Phase 7 covers this with a standalone test app first, and an optional guarded real-hardware install second.

---
# Part C: Phases
## Phases

Work one phase at a time. After each phase the human tests in the VM, pastes results, and `docs/PROGRESS.md` is updated.

### Phase 1: Environment and the sample provider
**Goal:** see a tile from a provider we built on the VM lock screen.
Tasks:
- Download and build Microsoft's SampleCredentialProvider (x64, VS 2022).
- Write `install/register.reg` and `install/unregister.reg` for the sample's CLSID.
- Write an install script and an uninstall script meant to be run inside the VM.
- Add a simple file logger (no secrets).
Done when: after registering in the VM and pressing Win+L, the sample tile appears; unregistering removes it; Password still works.

### Phase 2: Pattern tile and pattern window
**Goal:** our own grid window receives trackpad drag on the lock screen.
Tasks:
- Rename/clone the sample into our provider (new GUID, "Pattern" tile text).
- Tile has a button that opens the pattern window (via the provider events interface's window-creation callback).
- Draw a 3x3 grid; track press, drag, release; add dots with the skipped-dot rule; show the trail.
- Build the pattern window so it can ALSO run as a standalone desktop test app (`src/tools/pattern_window_demo`) with no provider install. This is needed for Phase 7.
- Log the resulting dot sequence to a debug log ONLY in a debug build (never in release).
Done when: on both the lock screen and the sign-in screen in the VM, dragging with the trackpad produces the right dot sequence.
If this fails, stop and report; see fallback in `PLAN.md` section 11.

### Phase 3: Enrollment and storage (baseline)
**Goal:** a pattern can be enrolled and verified.
Tasks:
- `src/common`: pattern encoding, PBKDF2-HMAC-SHA256 with random salt, constant-time compare, record format with version.
- Enrollment tool (elevated): checks the Windows password, records the pattern twice, enforces min 5 dots, writes the record to a
  SYSTEM/Administrators-only location.
- Provider reads the record and verifies a drawn pattern (still no unlock yet, just success/fail message).
- Persistent attempt counter and basic lockout (5 wrong -> 30 s).
Done when: right pattern shows success, wrong shows failure, lockout persists across VM reboot, a normal user cannot read the record.

### Phase 4: Real unlock
**Goal:** a correct pattern signs in.
Tasks:
- Encrypt the password with AES-GCM using the protected key; store in the record.
- On match, decrypt, build the serialized credential using the sample's packing helpers, return it, wipe memory.
- Support both the unlock-workstation and logon scenarios.
- If the stored password is stale or auth fails, show a message and fall back to the Password tile; stop auto-submitting.
Done when: in the VM, Win+L then pattern unlocks; after restart (and full password) pattern works per rules; changed password is handled gracefully.

### Phase 5: Hardening
Tasks:
- Escalating lockout (30 s, 1, 5, 15 min); disable Pattern after about 10 failures until a full-password login.
- Full password required after restart and after 24 h.
- TPM-backed key with pattern-derived authorization where available; fallback to baseline otherwise.
- Fail-closed error handling everywhere in the provider; verify no secrets in logs; hide-trail option.
Done when: all items in `docs/TESTING.md` sections "Lockout", "Full-password rule", "Failure handling", "Security checks" pass.

### Phase 6: Test, package, document
Tasks:
- Run the whole `docs/TESTING.md` checklist on the VM and at least one more machine/trackpad.
- Installer and uninstaller; signed build if distributing.
- Final README, security notes with honest limits, screenshots or a short demo.
Done when: checklist complete and `docs/PROGRESS.md` records results and known limitations.

### Phase 7: Real-trackpad tuning and optional real-hardware install
The VM only has a virtual mouse, so trackpad feel must be checked on real hardware.
Step 7a (safe, do this): run `pattern_window_demo` as a normal desktop app on the main PC (no provider installed).
- Test with tap-and-drag on and off, different pointer speeds, and palm contact.
- Tune hit radius, minimum dot spacing, and the start hint. Record the chosen values in `docs/PROGRESS.md`.
Step 7b (optional, risky): install the provider on a real machine ONLY if all of `docs/TESTING.md` passed in the VM and these safeguards exist:
- a second administrator account that works, a Windows restore point or full backup, the BitLocker recovery key saved offline, and you have rehearsed the Safe Mode removal from `docs/RECOVERY.md`.
- Prefer a spare machine over your main PC.
Done when: values tuned on the real trackpad, results logged, and (if 7b done) the real-hardware checklist passes.

---
# Part D: Security notes
## Security Notes

### Threat model
In scope:
- Someone guessing the pattern at the lock screen (physical access, online guessing).
- Someone watching the user draw it (shoulder surfing).
- A normal local user or malware without admin rights trying to read the stored secret.
- A stolen laptop with an offline disk.

Out of scope (be honest about this in the final README):
- An attacker with admin/SYSTEM rights on the running, unlocked machine.
- Hardware and firmware attacks.
- Anything that makes this stronger than a long password. It is not.

### Core trade-off
The provider must be able to produce the Windows password after a pattern match. So the password is stored in recoverable (encrypted) form.
Protection comes from: BitLocker/Device Encryption, access-locked storage, TPM binding, lockout, and requiring the full password regularly.

### Storage design
Record contents: format version, account name, salt, PBKDF2 hash of the pattern, encrypted password (AES-GCM, with nonce and tag),
attempt counter, lockout-until time, last full-password time.

Baseline:
- Random 256-bit key held in a store readable only by SYSTEM and Administrators (LSA secret or locked-down HKLM key).
- AES-GCM encryption of the password; a fresh random nonce per encryption.
- Pattern verified with PBKDF2-HMAC-SHA256, per-user random 16-byte salt, high iteration count, constant-time compare.

Hardened:
- TPM-backed key requiring an authorization value derived from the pattern, so guesses are rate-limited by hardware.
- Fall back to baseline if no TPM. Document which mode is active.

### Rules for code
- Never log the pattern, password, keys, salts-with-secrets, or decrypted data.
- SecureZeroMemory all sensitive buffers right after use; avoid copying them into std::string where it can be avoided.
- Constant-time comparison for hashes and tags.
- Validate every field read from the record; a corrupt record means Pattern is disabled, not a crash.
- Fail closed: any exception or error leads to hiding/declining the tile, never unlocking.
- No hardcoded keys or passwords. No debug backdoors in release builds.
- Lockout counters are persisted before the result of an attempt is shown.
- After one failed submission of the stored password (for example, it was changed), stop auto-submitting and require re-enrollment to avoid Windows account lockout.

### Known flaws and mitigations
See `PLAN.md` section 7. Keep that list updated as real tests reveal more.

---
# Part E: Test checklist
## Test Checklist

Run in the Windows 11 VM first (take a snapshot before each install). Tick each item and record results in `docs/PROGRESS.md`.

### Install / uninstall
- [ ] Register script makes the Pattern tile appear on the lock screen
- [ ] Unregister script removes it completely
- [ ] Password and PIN options unchanged and working after install

### Enrollment
- [ ] Wrong Windows password rejected
- [ ] Mismatched confirmation rejected
- [ ] Pattern under the minimum dots rejected
- [ ] Record unreadable by a normal (non-admin) user

### Pattern input
- [ ] Press-and-hold drag and tap-and-drag both work
- [ ] Skipped-dot rule matches the prototype (e.g. 0 to 8 adds 4; 0 to 2 adds 1)
- [ ] Hit targets feel forgiving on at least two different trackpads
- [ ] Works on lock screen AND sign-in screen after restart

### Unlock
- [ ] Correct pattern unlocks
- [ ] Wrong pattern does not unlock and increments the counter
- [ ] Reversed pattern does not unlock

### Lockout
- [ ] 5 wrong attempts triggers a 30 s delay; delays escalate
- [ ] Lockout state survives reboot
- [ ] After about 10 failures Pattern is disabled until full password login
- [ ] Counter clears after a full password login

### Full-password rule
- [ ] Pattern hidden after a restart until the password is used once
- [ ] Pattern hidden after the time limit without a password login

### Failure handling (Password option must still work in every case)
- [ ] Windows password changed after enrollment
- [ ] TPM missing or unavailable
- [ ] Record corrupted or deleted
- [ ] Pattern window fails to create
- [ ] Provider DLL missing or bad
- [ ] Safe Mode boots normally

### Scenarios
- [ ] Win+L lock screen
- [ ] Sign-in screen after restart
- [ ] Fast user switching
- [ ] Local account; then separately a Microsoft account (document results)

### Security checks
- [ ] No secrets in any log file
- [ ] Plaintext buffers wiped (code review)
- [ ] Offline disk cannot yield the password when BitLocker is on
- [ ] Code review of storage, serialization and lockout code done by the human

### Real hardware (Phase 7)
- [ ] Standalone pattern window tested on the real trackpad with tap-and-drag on and off
- [ ] Hit radius and spacing tuned; values recorded
- [ ] (Optional 7b) provider installed on a real machine only after safeguards in RECOVERY.md; lock, unlock and restart tests pass

---
# Part F: Recovery plan
## Recovery Plan (read before installing anything)

Before every install in the VM:
1. Take a VM snapshot (name it with the date and phase).
2. Make sure a second administrator account exists and you know its password.
3. Back up the BitLocker recovery key if BitLocker is on.

If sign-in breaks:
1. **Revert the VM snapshot.** This is the fastest fix.
2. Or boot into Safe Mode (Safe Mode normally loads only the default providers) and run `install/unregister.reg`, or delete the provider's
   key under `HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\Authentication\Credential Providers\{our GUID}`.
3. Or sign in with the second admin account and run the uninstall script.
4. Or use Windows Recovery Environment, open Command Prompt, and remove the registry key from the offline hive.

Never:
- Install an untested build on your main PC.
- Delete or modify built-in providers.
- Skip the snapshot.

### Extra rules for a real (non-VM) machine
- Do this only after the whole VM test pass and only in Phase 7b.
- Before installing: a second working admin account, a restore point or full backup, the BitLocker recovery key saved offline, the Safe Mode removal steps rehearsed in the VM.
- Safe Mode: on the sign-in screen hold Shift, click Power > Restart, then Troubleshoot > Advanced options > Startup Settings > Restart > press 4.
- If locked out completely: boot the Windows Recovery Environment, open Command Prompt, and remove the provider key from the offline registry (needs the BitLocker key if encrypted).

---
# Part G: Codex rules (AGENTS.md)
## AGENTS.md - instructions for the coding agent

### Project
Build an Android-style 3x3 pattern sign-in for the Windows lock and sign-in screen, drawn with the laptop trackpad.
Mechanism: a Windows Credential Provider (64-bit C++ DLL loaded by LogonUI.exe). After a pattern match, the provider
returns the user's stored Windows password to Windows in a serialized credential. Windows performs the real authentication.
Read `PLAN.md` first, then `docs/PHASES.md`, `docs/SECURITY.md`.

### Hard safety rules (never break these)
1. **VM only.** Never run install, register, or registry-modifying commands on the host machine. Produce scripts and let the
   human run them inside the Windows 11 VM.
2. **Never remove, replace, or filter the built-in Password tile or other providers.** Our provider only ADDS a tile.
3. **Fail closed.** Any error inside the provider must result in the Pattern tile being hidden or declining, never a crash of
   LogonUI and never an unlock.
4. **No secrets in logs, ever.** Never log the pattern, the password, keys, or decrypted buffers.
5. **No hardcoded credentials, keys, or test passwords** in source files.
6. **Wipe sensitive buffers** (SecureZeroMemory) immediately after use. Compare hashes in constant time.
7. **Do not invent Windows APIs.** If you are not sure an interface, flag, or function exists or how it behaves, check Microsoft's
   official documentation and the official SampleCredentialProvider, and say which source you used. If still unsure, ask.
8. **No unverified security claims.** Do not write comments or docs claiming the design is "secure" or "unbreakable".

### Working method
- Work on ONE phase at a time, the one the human names. Do not start the next phase.
- Start every phase with a short plan (files to touch, risks, how it will be tested). Wait for approval if the plan changes
  anything in `PLAN.md`.
- Make small, reviewable commits with clear messages. Never rewrite history.
- When done with a phase, update `docs/PROGRESS.md` with: what was built, how to test it in the VM, what is NOT verified.
- Clearly separate "I wrote this" from "this has been verified on a real lock screen". Only the human can verify the latter.
- When the human pastes Event Viewer errors or logs, diagnose from them; do not guess blindly.
- Explain security-relevant code (storage, serialization, lockout) in plain language after writing it.
- If something in `PLAN.md` looks wrong or impossible, stop and say so rather than silently working around it.

### Tech and layout
- Language: C++17 for the provider DLL (x64 only). Enrollment tool: C# (.NET WPF) or C++, human's choice at Phase 3.
- Tools: Visual Studio 2022, Windows SDK, MSBuild/CMake as the sample uses.
- Base: Microsoft's SampleCredentialProvider (Windows-classic-samples). Keep its COM structure.

```
src/provider/      Credential Provider DLL (tile, pattern window, verification, serialization)
src/enroll/        Enrollment tool (elevated, runs on the normal desktop)
src/common/        Shared code: pattern logic, PBKDF2, record format, lockout
install/           register.reg, unregister.reg, install and uninstall scripts (for the VM)
docs/              Plan, security, testing, recovery, progress
prototype/         Phase 0 Python prototype (reference for pattern logic)
```

### Pattern rules (must match the prototype)
- 3x3 grid, dots 0-8 left-to-right, top-to-bottom. Each dot used once. Order matters.
- Minimum 5 dots (configurable constant). Skipped-dot rule: passing over an unvisited dot adds it automatically.
- Verification: PBKDF2-HMAC-SHA256 with a per-user random 16-byte salt, constant-time compare.
- Lockout: 5 wrong attempts -> 30 s, then escalating (1, 5, 15 min); after about 10, Pattern disabled until a full password login.
  Counter stored persistently (survives reboot).
- Full password required after every restart and after 24 hours without one.

### Definition of done for any phase
- Builds cleanly with no warnings you can fix.
- The phase's "Done when" checks in `docs/PHASES.md` are listed with how the human should test each in the VM.
- `docs/PROGRESS.md` updated.
- No rule above violated.

### Environment (so you do not assume wrongly)
- The human runs you on their MAIN PC. You write and commit code; you do NOT build for testing, install, or register anything here.
- All building, installing and lock-screen testing happens in a Windows 11 Pro VM (VMware Workstation Pro) where the human pulls the repo with git.
- The VM has a virtual mouse, not the real trackpad. Do not claim trackpad feel is verified until Phase 7.
- Visual Studio 2022 in the VM has the Desktop development with C++ workload; prefer static runtime linking (/MT) for the DLL and say if you choose otherwise.
- The pattern window must be buildable as a standalone desktop test app (`src/tools/pattern_window_demo`) in addition to being used by the provider.
- Test accounts in the VM use throwaway passwords. Never ask for or store the human's real Windows password.
- Phase 7b (real-hardware install) only happens if the human explicitly asks and the safeguards in `docs/RECOVERY.md` are in place.

---
# Part H: Prompts
## Start prompt (paste into Codex; model: newest flagship, reasoning effort: HIGH for this first planning step)

You are helping me build a Windows Credential Provider that adds an Android-style 3x3 pattern sign-in drawn with the laptop trackpad.

First read these files completely: AGENTS.md, PLAN.md, docs/PHASES.md, docs/SECURITY.md, docs/RECOVERY.md, docs/TESTING.md, docs/SETUP_GUIDE.md, docs/PROGRESS.md.
Also look at prototype/pattern_unlock.py, which is the reference for the pattern logic.

Environment: you run on my main PC and only write and commit code. I build and test in a Windows 11 Pro VM (VMware) by pulling the repo with git. The VM has a virtual mouse, not my real trackpad.

Rules that matter most: all installs and registry changes happen only in my Windows 11 virtual machine, never on this host; our provider only
adds a tile and never touches the Password tile; never log secrets; do not invent Windows APIs, check Microsoft's official docs and the
official SampleCredentialProvider and tell me which source you used.

Do not write any code yet. Step 1: reply with
1. A short summary of the project in your own words, so I can check you understood it.
2. Any problems, risks or contradictions you see in PLAN.md.
3. A detailed plan for Phase 1 only (files you will create, commands, how I will test it in the VM, what could go wrong).
Then wait for my approval before implementing.
## Phase prompts

Use one at a time. Recommended effort: plan on HIGH, then switch to MEDIUM to implement. After each phase, test in the VM, paste results
back, and make sure docs/PROGRESS.md is updated before starting the next.

---
### Phase 1: Environment and sample provider
Approved. Implement Phase 1 only, as described in docs/PHASES.md.
- Download and build Microsoft's SampleCredentialProvider (x64, Visual Studio 2022) into src/provider/.
- Create install/register.reg, install/unregister.reg and install and uninstall scripts for the sample's CLSID. They are for the VM only; do not run them here.
- Add a simple file logger that never logs secrets.
- Give me exact step-by-step instructions to build, register and test it in my VM, and what I should see.
- Update docs/PROGRESS.md.
Stop after Phase 1 and wait for my test results.

---
### Phase 2: Pattern tile and window
Phase 1 passed: [paste what you saw]. Implement Phase 2 only.
- Clone the sample into our own provider with a new GUID and a tile named Pattern.
- The tile opens a pattern window using the provider's window-creation callback. The window draws the 3x3 grid, tracks press/drag/release, applies the skipped-dot rule, and shows the trail.
- Also make the pattern window buildable as a standalone desktop app in src/tools/pattern_window_demo, so I can test it on my real trackpad later without installing the provider.
- Debug builds may log the dot sequence to a file; release builds must not.
- Tell me exactly how to test on the lock screen and the sign-in screen after restart.
If the window does not receive trackpad input, stop and propose the fallback from PLAN.md section 11.

---
### Phase 3: Enrollment and storage
Phase 2 passed: [results]. Implement Phase 3 only.
- src/common: pattern encoding, PBKDF2-HMAC-SHA256, random salt, constant-time compare, versioned record format.
- Enrollment tool (elevated): verify the Windows password, enter the pattern twice, minimum 5 dots, write the record to a SYSTEM/Administrators-only location.
- Provider verifies a drawn pattern and shows success or failure (no unlock yet). Persistent attempt counter with 5 wrong -> 30 s lockout.
- After writing the code, explain the storage and lockout code in plain language and list what you did not verify.

---
### Phase 4: Real unlock
Phase 3 passed: [results]. Implement Phase 4 only.
- Encrypt the password with AES-GCM under the protected key. On a match, decrypt, build the serialized credential with the sample's helpers, return it, and wipe memory.
- Support unlock-workstation and logon scenarios.
- If auth fails or the stored password is stale, show a message, fall back to the Password tile, stop auto-submitting.
- Review your own serialization and storage code against docs/SECURITY.md and list any concerns.

---
### Phase 5: Hardening
Phase 4 passed: [results]. Implement Phase 5 only.
- Escalating lockout (30 s, 1, 5, 15 min); Pattern disabled after about 10 failures until a full-password login.
- Full password required after restart and after 24 h.
- TPM-backed key with pattern-derived authorization where available, fallback to baseline otherwise (research the official Platform Crypto Provider docs first and cite them).
- Fail-closed error handling throughout; hide-trail option; confirm no secrets are logged.

---
### Phase 6: Security review, test and package
Phase 5 passed: [results]. Use HIGH effort.
- Do a security review of the whole codebase against docs/SECURITY.md and PLAN.md section 7. List findings by severity with file and line references.
- Fix what you can. Then write the installer and uninstaller, the final README with honest limitations, and update docs/TESTING.md with anything new.
- Do not claim the system is secure. Describe what it does and does not protect against.

---
### When something breaks (use at any phase, HIGH effort)
The VM shows this problem: [describe]. Event Viewer / log output: [paste]. Last change you made: [describe].
Diagnose from the evidence first. Give the most likely causes ranked, the smallest change to test each, and how I recover the VM if needed. Do not rewrite unrelated code.

---
### Phase 7: Real-trackpad tuning
Phase 6 passed: [results]. Implement Phase 7a only.
- Make sure src/tools/pattern_window_demo builds as a normal desktop app and exposes the tunable values (hit radius, minimum dot spacing, start hint) in one config place.
- Add a simple on-screen readout of the dot sequence for tuning (debug build only).
- Give me a short test script: tap-and-drag on and off, different pointer speeds, palm contact.
Do not touch the provider install. Phase 7b (real-hardware install) only if I explicitly ask later.

---
# Part I: Progress log template
## Progress Log

Update after every session. Keep it honest: separate "written" from "verified on a real lock screen".

### Template
#### YYYY-MM-DD: Phase N
- Built:
- Tested by human in VM (results):
- Not verified yet:
- Problems / error messages:
- Decisions made:
- Next step:

### Log
#### Phase 0: done
- Built: Python prototype (`prototype/pattern_unlock.py`) with 3x3 grid, skipped-dot rule, salted hash, verification, lockout.
- Verified: hashing and skipped-dot logic tested; GUI to be run on the laptop.
- Not verified: anything on the real Windows lock screen.