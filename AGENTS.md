# AGENTS.md - instructions for the coding agent

## Project
Build an Android-style 3x3 pattern sign-in for the Windows lock and sign-in screen, drawn with the laptop trackpad.
Mechanism: a Windows Credential Provider (64-bit C++ DLL loaded by LogonUI.exe). After a pattern match, the provider
returns the user's stored Windows password to Windows in a serialized credential. Windows performs the real authentication.
Read `PLAN.md` first, then `docs/PHASES.md`, `docs/SECURITY.md`.

## Hard safety rules (never break these)
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

## Working method
- Work on ONE phase at a time, the one the human names. Do not start the next phase.
- Start every phase with a short plan (files to touch, risks, how it will be tested). Wait for approval if the plan changes
  anything in `PLAN.md`.
- Make small, reviewable commits with clear messages. Never rewrite history.
- When done with a phase, update `docs/PROGRESS.md` with: what was built, how to test it in the VM, what is NOT verified.
- Clearly separate "I wrote this" from "this has been verified on a real lock screen". Only the human can verify the latter.
- When the human pastes Event Viewer errors or logs, diagnose from them; do not guess blindly.
- Explain security-relevant code (storage, serialization, lockout) in plain language after writing it.
- If something in `PLAN.md` looks wrong or impossible, stop and say so rather than silently working around it.

## Tech and layout
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

## Pattern rules (must match the prototype)
- 3x3 grid, dots 0-8 left-to-right, top-to-bottom. Each dot used once. Order matters.
- Minimum 5 dots (configurable constant). Skipped-dot rule: passing over an unvisited dot adds it automatically.
- Verification: PBKDF2-HMAC-SHA256 with a per-user random 16-byte salt, constant-time compare.
- Lockout: 5 wrong attempts -> 30 s, then escalating (1, 5, 15 min); after about 10, Pattern disabled until a full password login.
  Counter stored persistently (survives reboot).
- Full password required after every restart and after 24 hours without one.

## Definition of done for any phase
- Builds cleanly with no warnings you can fix.
- The phase's "Done when" checks in `docs/PHASES.md` are listed with how the human should test each in the VM.
- `docs/PROGRESS.md` updated.
- No rule above violated.

## Environment (so you do not assume wrongly)
- The human runs you on their MAIN PC. You write and commit code; you do NOT build for testing, install, or register anything here.
- All building, installing and lock-screen testing happens in a Windows 11 Pro VM (VMware Workstation Pro) where the human pulls the repo with git.
- The VM has a virtual mouse, not the real trackpad. Do not claim trackpad feel is verified until Phase 7.
- Visual Studio 2022 in the VM has the Desktop development with C++ workload; prefer static runtime linking (/MT) for the DLL and say if you choose otherwise.
- The pattern window must be buildable as a standalone desktop test app (`src/tools/pattern_window_demo`) in addition to being used by the provider.
- Test accounts in the VM use throwaway passwords. Never ask for or store the human's real Windows password.
- Phase 7b (real-hardware install) only happens if the human explicitly asks and the safeguards in `docs/RECOVERY.md` are in place.
