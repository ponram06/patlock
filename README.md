# Trackpad Pattern Unlock for Windows (work in progress)

An Android-style 3x3 pattern sign-in for the real Windows lock screen, drawn with the laptop trackpad.
Planned as a Windows **Credential Provider** (C++ DLL) plus an enrollment tool and a protected credential store.

Current code is a **Phase 1 Microsoft sample-provider spike**, not a working
pattern sign-in product. The sample source, x64 CMake build and VM-only scripts
are written but have not been compiled or tested in a VM. Start with
[Phase 1 VM instructions](docs/PHASE1_VM.md). See [progress](docs/PROGRESS.md)
and [the unresolved password-refresh design](docs/PASSWORD_REFRESH_DECISION.md).
The original FINAL_REPORT.md is a planning package, not evidence of completion.

## Read in this order
1. `FINAL_REPORT.md`: everything in ONE file (setup, plan, phases, security, tests, recovery, prompts).
2. `docs/SETUP_GUIDE.md`: step-by-step from zero (VMware VM, tools, git, Codex).
3. `docs/RECOVERY.md`: read before installing anything.

## Folder guide
| File | Purpose |
| --- | --- |
| `FINAL_REPORT.md` | The complete master document |
| `AGENTS.md` | Rules the coding agent must follow (Codex reads it automatically) |
| `PLAN.md` | Architecture, pattern rules, storage, flaws, phases |
| `docs/SETUP_GUIDE.md` | Environment setup and the daily workflow |
| `docs/PHASES.md` | Phases 1 to 7 with "done when" checks |
| `docs/SECURITY.md` | Threat model and security rules |
| `docs/TESTING.md` | Test checklist |
| `docs/RECOVERY.md` | How to recover if sign-in breaks |
| `docs/PROGRESS.md` | Running log, updated after every session |
| `prompts/START_PROMPT.md` | First prompt for Codex |
| `prompts/PHASE_PROMPTS.md` | One prompt per phase, plus a "something broke" prompt |
| `prototype/pattern_unlock.py` | Phase 0 Python prototype |
| `.gitignore` | Keeps build output, logs and secrets out of git |

## The whole process in 12 steps
1. Check virtualization is enabled and you have 120 GB free (SETUP_GUIDE section 1).
2. Download the free Windows 11 ISO (install Pro, no key).
3. Create the VMware VM: 8 GB RAM, 4 cores, 80 GB disk, TPM (save the encryption password).
4. Install Windows with a throwaway account; install VMware Tools; create `admin2`; snapshot `clean`.
5. In the VM install Visual Studio 2022 (Desktop development with C++), git; pause updates; snapshot `tools-ready`.
6. On the main PC unzip this folder, `git init`, commit, push to a PRIVATE GitHub repo; `git clone` in the VM.
7. Open the folder in Codex (newest flagship model). Paste `prompts/START_PROMPT.md` at HIGH effort.
8. Approve the plan, then run the Phase 1 prompt at MEDIUM effort.
9. Push, then in the VM snapshot, pull, build, register, lock, test. Paste results back to Codex.
10. Repeat for Phases 2 to 6, snapshotting `phaseN-pass` each time. Update `docs/PROGRESS.md`.
11. Phase 7: tune on the real trackpad with the standalone test app. Real-hardware install only with safeguards.
12. Finish with the full `docs/TESTING.md` checklist and an honest README about limits.

## Golden rules
- Install and register the provider ONLY in the VM (until Phase 7b).
- The Password sign-in option must always keep working.
- Never use your real Windows password in the VM.
- Snapshot before every install.
- You run the lock-screen tests; the agent cannot see your screen. Paste evidence back to it.
