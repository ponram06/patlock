# Security policy

Patlock runs inside the Windows sign-in screen and stores a recoverable copy of a Windows
password, so security reports are very welcome.

## Reporting a vulnerability
Please report privately through GitHub: **Security > Report a vulnerability** on this
repository (GitHub private vulnerability reporting). Do not open a public issue for
security problems.

Include the Windows version, whether a TPM is present, the steps to reproduce, and what you
expected. Never include real passwords or patterns.

## Supported versions
Only the latest commit on `main` is supported. Patlock is experimental: it has been tested
only in a Windows 11 virtual machine, the binaries are not code-signed, and it has had no
independent security review.

## Design and known limits
See [docs/SECURITY.md](docs/SECURITY.md) for the threat model and
[docs/CURRENT_STORAGE.md](docs/CURRENT_STORAGE.md) for the storage format.
