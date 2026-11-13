# Phase prompts

Use one at a time. Recommended effort: plan on HIGH, then switch to MEDIUM to implement. After each phase, test in the VM, paste results
back, and make sure docs/PROGRESS.md is updated before starting the next.

---
## Phase 1: Environment and sample provider
Approved. Implement Phase 1 only, as described in docs/PHASES.md.
- Download and build Microsoft's SampleCredentialProvider (x64, Visual Studio 2022) into src/provider/.
- Create install/register.reg, install/unregister.reg and install and uninstall scripts for the sample's CLSID. They are for the VM only; do not run them here.
- Add a simple file logger that never logs secrets.
- Give me exact step-by-step instructions to build, register and test it in my VM, and what I should see.
- Update docs/PROGRESS.md.
Stop after Phase 1 and wait for my test results.

---
## Phase 2: Pattern tile and window
Phase 1 passed: [paste what you saw]. Implement Phase 2 only.
- Clone the sample into our own provider with a new GUID and a tile named Pattern.
- The tile opens a pattern window using the provider's window-creation callback. The window draws the 3x3 grid, tracks press/drag/release, applies the skipped-dot rule, and shows the trail.
- Also make the pattern window buildable as a standalone desktop app in src/tools/pattern_window_demo, so I can test it on my real trackpad later without installing the provider.
- Debug builds may log the dot sequence to a file; release builds must not.
- Tell me exactly how to test on the lock screen and the sign-in screen after restart.
If the window does not receive trackpad input, stop and propose the fallback from PLAN.md section 11.

---
## Phase 3: Enrollment and storage
Phase 2 passed: [results]. Implement Phase 3 only.
- src/common: pattern encoding, PBKDF2-HMAC-SHA256, random salt, constant-time compare, versioned record format.
- Enrollment tool (elevated): verify the Windows password, enter the pattern twice, minimum 5 dots, write the record to a SYSTEM/Administrators-only location.
- Provider verifies a drawn pattern and shows success or failure (no unlock yet). Persistent attempt counter with 5 wrong -> 30 s lockout.
- After writing the code, explain the storage and lockout code in plain language and list what you did not verify.

---
## Phase 4: Real unlock
Phase 3 passed: [results]. Implement Phase 4 only.
- Encrypt the password with AES-GCM under the protected key. On a match, decrypt, build the serialized credential with the sample's helpers, return it, and wipe memory.
- Support unlock-workstation and logon scenarios.
- If auth fails or the stored password is stale, show a message, fall back to the Password tile, stop auto-submitting.
- Review your own serialization and storage code against docs/SECURITY.md and list any concerns.

---
## Phase 5: Hardening
Phase 4 passed: [results]. Implement Phase 5 only.
- Escalating lockout (30 s, 1, 5, 15 min); Pattern disabled after about 10 failures until a full-password login.
- Full password required after restart and after 24 h.
- TPM-backed key with pattern-derived authorization where available, fallback to baseline otherwise (research the official Platform Crypto Provider docs first and cite them).
- Fail-closed error handling throughout; hide-trail option; confirm no secrets are logged.

---
## Phase 6: Security review, test and package
Phase 5 passed: [results]. Use HIGH effort.
- Do a security review of the whole codebase against docs/SECURITY.md and PLAN.md section 7. List findings by severity with file and line references.
- Fix what you can. Then write the installer and uninstaller, the final README with honest limitations, and update docs/TESTING.md with anything new.
- Do not claim the system is secure. Describe what it does and does not protect against.

---
## When something breaks (use at any phase, HIGH effort)
The VM shows this problem: [describe]. Event Viewer / log output: [paste]. Last change you made: [describe].
Diagnose from the evidence first. Give the most likely causes ranked, the smallest change to test each, and how I recover the VM if needed. Do not rewrite unrelated code.

---
## Phase 7: Real-trackpad tuning
Phase 6 passed: [results]. Implement Phase 7a only.
- Make sure src/tools/pattern_window_demo builds as a normal desktop app and exposes the tunable values (hit radius, minimum dot spacing, start hint) in one config place.
- Add a simple on-screen readout of the dot sequence for tuning (debug build only).
- Give me a short test script: tap-and-drag on and off, different pointer speeds, palm contact.
Do not touch the provider install. Phase 7b (real-hardware install) only if I explicitly ask later.
