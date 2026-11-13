# Phases

Work one phase at a time. After each phase the human tests in the VM, pastes results, and `docs/PROGRESS.md` is updated.

## Phase 1: Environment and the sample provider
**Goal:** see a tile from a provider we built on the VM lock screen.
Tasks:
- Download and build Microsoft's SampleCredentialProvider (x64, VS 2022).
- Write `install/register.reg` and `install/unregister.reg` for the sample's CLSID.
- Write an install script and an uninstall script meant to be run inside the VM.
- Add a simple file logger (no secrets).
Done when: after registering in the VM and pressing Win+L, the sample tile appears; unregistering removes it; Password still works.

## Phase 2: Pattern tile and pattern window
**Goal:** our own grid window receives trackpad drag on the lock screen.
Tasks:
- Rename/clone the sample into our provider (new GUID, "Pattern" tile text).
- Tile has a button that opens the pattern window (via the provider events interface's window-creation callback).
- Draw a 3x3 grid; track press, drag, release; add dots with the skipped-dot rule; show the trail.
- Build the pattern window so it can ALSO run as a standalone desktop test app (`src/tools/pattern_window_demo`) with no provider install. This is needed for Phase 7.
- Never log dot sequences, including debug builds (AGENTS.md secret-logging rule).
Done when: on both the lock screen and the sign-in screen in the VM, dragging with the trackpad produces the right dot sequence.
If this fails, stop and report; see fallback in `PLAN.md` section 11.

## Phase 3: Enrollment and storage (baseline)
**Goal:** a pattern can be enrolled and verified.
Tasks:
- `src/common`: pattern encoding, PBKDF2-HMAC-SHA256 with random salt, constant-time compare, record format with version.
- Enrollment tool (elevated): checks the Windows password, records the pattern twice, enforces min 5 dots, writes the record to a
  SYSTEM/Administrators-only location.
- Provider reads the record and verifies a drawn pattern (still no unlock yet, just success/fail message).
- Persistent attempt counter and basic lockout (5 wrong -> 30 s).
Done when: right pattern shows success, wrong shows failure, lockout persists across VM reboot, a normal user cannot read the record.

## Phase 4: Real unlock
**Goal:** a correct pattern signs in.
Tasks:
- Encrypt the password with AES-GCM using the protected key; store in the record.
- On match, decrypt, build the serialized credential using the sample's packing helpers, return it, wipe memory.
- Support both the unlock-workstation and logon scenarios.
- If the stored password is stale or auth fails, show a message and fall back to the Password tile; stop auto-submitting.
Done when: in the VM, Win+L then pattern unlocks; after restart (and full password) pattern works per rules; changed password is handled gracefully.

## Phase 5: Hardening
Tasks:
- Escalating lockout (30 s, 1, 5, 15 min); disable Pattern after about 10 failures until explicit Patlock password verification.
- Current approved Option A in PLAN.md permits Pattern across reboots; password
  age expiration defaults off. Password refresh resets lockout; re-enrollment
  recovers stale or interrupted stored-password submissions.
- TPM-backed key with pattern-derived authorization where available; fallback to baseline otherwise.
- Fail-closed error handling everywhere in the provider; verify no secrets in logs; hide-trail option.
Done when: all items in `docs/TESTING.md` sections "Lockout", "Full-password rule", "Failure handling", "Security checks" pass.

## Phase 6: Test, package, document
Tasks:
- Run the whole `docs/TESTING.md` checklist on the VM and at least one more machine/trackpad.
- Installer and uninstaller; signed build if distributing.
- Final README, security notes with honest limits, screenshots or a short demo.
Done when: checklist complete and `docs/PROGRESS.md` records results and known limitations.

## Phase 7: Real-trackpad tuning and optional real-hardware install
The VM only has a virtual mouse, so trackpad feel must be checked on real hardware.
Step 7a (safe, do this): run `pattern_window_demo` as a normal desktop app on the main PC (no provider installed).
- Test with tap-and-drag on and off, different pointer speeds, and palm contact.
- Tune hit radius, minimum dot spacing, and the start hint. Record the chosen values in `docs/PROGRESS.md`.
Step 7b (optional, risky): install the provider on a real machine ONLY if all of `docs/TESTING.md` passed in the VM and these safeguards exist:
- a second administrator account that works, a Windows restore point or full backup, the BitLocker recovery key saved offline, and you have rehearsed the Safe Mode removal from `docs/RECOVERY.md`.
- Prefer a spare machine over your main PC.
Done when: values tuned on the real trackpad, results logged, and (if 7b done) the real-hardware checklist passes.
