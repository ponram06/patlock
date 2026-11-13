# Implementation ledger

## Authorization and constraints (2026-11-13)

The user requested sequential implementation of all phases without waiting between
features, and authorized feature commits and pushes to
https://github.com/ponram06/patlock.git on main. VM and real-hardware acceptance
remain human tasks. Do not build, run provider tests, install or register on the
development host. Do not claim a phase meets its Done when checks without VM
evidence. PLAN.md changes still require approval under AGENTS.md.

## Phase 1 plan

- Import Microsoft's CredentialProvider V2 sample with its license and exact
  upstream revision into src/provider, preserving its COM structure.
- Add x64-only CMake build configuration targeting C++17 and the static runtime.
- Add install/register.reg, install/unregister.reg and PowerShell installation
  and recovery scripts restricted to the project's provider identity and VM use.
- Add fixed-message diagnostics without accepting secret-bearing text.
- Document exact VM build, registration, Password fallback and removal checks.
- Inspect source and script syntax on the host; leave compilation, tests and
  registry operations to the VM. The principal risk is destabilizing LogonUI.

## Pending phases

2. Pattern-only tile and a shared grid window plus desktop demo; minimum five
   dots, skipped dots, cancellation, no serialization yet and no pattern logs.
3. Native C++ enrollment (implementation choice), password validation, versioned
   storage, PBKDF2 verification and persisted attempts.
4. AES-GCM password recovery and Windows credential serialization; stale-password
   failure disables subsequent submissions.
5. Escalating lockout, full-password freshness and TPM spike. Verify the actual
   mechanism for recognizing a full password login; do not infer it from any
   desktop login or unlock. Resolve design gaps before claiming compliance.
6. Packaging, full VM test instructions and honest limitations.
7. Desktop-demo tuning instructions; hardware results are pending the human.

## Decisions

- AGENTS.md's absolute prohibition on secret logging overrides the Phase 2
  request to log dot sequences in debug builds.
- The new five-dot requirement overrides the prototype's older MIN_DOTS = 4.
- No recurring automation is installed to monitor this task's completion or
  usage limits. Progress is checkpointed in the repository.

## Execution

- Initial inspection: no commits, no configured remote, no C++ implementation.
- Phase 1: source and VM scripts written; static inspection only. No builds or
  VM tests have been run. Full acceptance remains pending.
- Noninteractive GitHub access failed because Git could not obtain credentials.
- Password freshness design decision is pending in PASSWORD_REFRESH_DECISION.md.
  Do not silently implement a substitute for built-in Password login detection.
