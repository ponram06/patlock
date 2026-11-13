# Release status: 2026-11-13

Source checkpoint only; market-ready acceptance is not complete.

The audit repair commit 989205f was pushed successfully to origin/main.
The subsequent source repair uses record version 5: re-enrollment is required
inside the VM. TPM key names now include the random enrollment nonce, and
creation no longer deletes an existing key. Failed creation therefore cannot
destroy the prior generation. Old/orphan generations are deliberately retained;
safe garbage collection and transactional record replacement remain pending.
TPM deletion errors are propagated.

Returning a decrypted password now durably marks its record disabled with the
matching submission token. Successful ReportResult clears it; failed or absent
completion leaves it disabled. Repeated successful unlock and crash boundaries
must be tested in the VM before accepting this protocol.

Still required before release:

- Registry commit/flush fault handling and transactional key retirement.
- Official API validation and VM/hardware tests of PCP authorization and TPM
  anti-hammering; current provider availability is not proof of these properties.
- DPI-aware layout, accessible controls and keyboard/screen-reader testing.
- Actual VM build/CTest, lock-screen fault tests, trackpad tuning, signing,
  installer upgrade/recovery and independent security review.

The development host must not be used to build or execute provider tests. No
source review or successful Git push substitutes for the outstanding evidence.
