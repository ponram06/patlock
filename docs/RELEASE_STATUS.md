# Release status: 2026-11-13

The release-candidate implementation pass is written. Market-ready acceptance
is still pending the human's VM/hardware tests and distribution signing.
**Record format 6 requires re-enrollment in the VM.**

Implemented in this pass:

- KTM transactional record writes/removal; pending submission tokens and disabled
  state precede plaintext release; nullable outputs and COM bounds are handled.
- Unique TPM generations, failed-new-generation cleanup, postcommit old-key
  retirement and cleanup warnings; only genuinely absent TPM selects baseline.
- Official PCPTool UTF-16 PIN path, silent key operations, key policy/DACL
  requests and authorized reopen/wrapping roundtrip. These do not prove hardware
  authorization/anti-hammering enforcement; TPM_VALIDATION.md lists needed tests.
- Deliberate press-drag-release; DPI/monitor bounds, high contrast, native
  keyboard fallback/status controls and authoritative timed-lockout countdown.
- Timer/credential lifetime guards, exception containment, no hidden password
  getter exposure, and removal of hardcoded password advice.
- Password-authenticated enrollment removal; guarded VM build/test script,
  five executable test targets, x64 VS2022/C++17/static-runtime project support,
  non-destructive packaging with public SHA-256 artifact hashes.

Static verification: source/code review, whitespace checks, PowerShell syntax and
project XML parsing. No host build, executable tests, TPM access, enrollment,
registration or installation occurred. Old user-confirmed VM unlocking does not
verify the new candidate.

Next action: follow **VM_ACCEPTANCE.md**, including repeat unlock, stale-password,
crash/transaction faults, TPM negative authorization, monitor/accessibility and
trackpad cases. Record actual results in PROGRESS.md. For distribution, apply the
publisher's signing certificate and perform independent security review.

The grid is pointer input; keyboard/screen-reader users retain native Password
fallback. A crash during TPM enrollment can leave an unused historical key;
unknown keys must not be deleted indiscriminately. No security certification,
forensic deletion or hardware anti-hammering guarantee is asserted.
