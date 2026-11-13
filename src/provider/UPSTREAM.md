# Microsoft sample provenance

Source: https://github.com/microsoft/Windows-classic-samples/tree/434f6002bdf9cf9829406c3ff2b33387982d6168/Samples/CredentialProvider/cpp

Revision: `434f6002bdf9cf9829406c3ff2b33387982d6168`.
Retrieved 2026-11-13. License: LICENSE.microsoft in this directory.

This is the Windows V2 sample, still using its original CLSID
`{5fd3d285-0dd9-4362-8855-e0abaacd4af6}` for the Phase 1 spike.
It is not yet a pattern credential provider. The original solution/project
files are retained for reference; use the root CMake project for VS 2022, C++17,
x64 and static runtime (/MT, /MTd in Debug).

Do not use the original registration files directly. The guarded scripts under
install/ are the intended VM-only entry points.

Local modifications: initialized provider state and output pointers, decline
enumeration when credential creation fails, nonthrowing class allocation,
preserved static CRT thread notifications, fixed-message diagnostic logging,
password-buffer cleanup, and resource-file cleanup. helpers.cpp is modified
to wipe password copies on cleanup. None of these changes has been VM-tested.
