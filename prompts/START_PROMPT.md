# Start prompt (paste into Codex; model: newest flagship, reasoning effort: HIGH for this first planning step)

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
