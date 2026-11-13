# Complete Setup Guide: from zero to Phase 1

## 0. The big picture
You use TWO environments and never mix them up.

| | Main PC (your real laptop) | Windows 11 VM (VMware Workstation Pro) |
| --- | --- | --- |
| Runs | Codex, git, your editor | Visual Studio, build, install, lock-screen tests |
| Does | Writes and commits code, pushes to GitHub | Pulls code, builds, registers the provider, locks, tests |
| Never does | Install or register the provider (until Phase 7) | Hold your real password or real data |

Flow: `Codex (main PC) -> git push -> GitHub (private) -> git pull (VM) -> build -> test -> paste results back to Codex`.

## 1. Pre-flight checklist (finish this before Phase 1)
- [ ] Virtualization is on: Task Manager > Performance > CPU shows "Virtualization: Enabled" (if not, enable it in BIOS/UEFI).
- [ ] At least 120 GB free disk (ISO 6-8 GB, VM disk up to 80 GB, snapshots grow too).
- [ ] RAM: give the VM 8 GB; close extra browser tabs while it runs.
- [ ] VMware Workstation Pro installed.
- [ ] GitHub account, and a private repository created (empty).
- [ ] Codex access with your ChatGPT Pro account.
- [ ] You have read `docs/RECOVERY.md`.
- [ ] If the VM is very slow or VMware complains about Hyper-V or Core isolation, search "VMware Workstation Hyper-V compatibility"; it is a known setup issue, not a project problem.

## 2. Get the Windows 11 ISO
Recommended: the regular free Windows 11 ISO from Microsoft's Windows 11 download page ("Download Windows 11 Disk Image (ISO) for x64 devices", the multi-edition ISO).
- During setup choose "I don't have a product key" and pick **Windows 11 Pro**.
- It runs unactivated with no time limit (watermark and locked personalization only), which suits a test VM.

Alternative: the Windows 11 Enterprise **evaluation** ISO from Microsoft's Evaluation Center. It works for 90 days, then nags and shuts down hourly.
Do NOT use Microsoft's old prebuilt "development environment" VM: the downloadable image was an expired 2024 evaluation build.

## 3. Create the VM in VMware Workstation Pro
1. File > New Virtual Machine > **Typical** > select the Windows 11 ISO.
2. If Easy Install appears: enter a name and a **throwaway password** (this makes a local account; no Microsoft account needed). Choose Windows 11 Pro if asked.
3. Guest OS: Microsoft Windows > Windows 11 x64.
4. Encryption: Windows 11 needs a virtual TPM, so VMware asks you to encrypt the VM. Choose to encrypt **only the files needed to support the TPM**.
   **Write the encryption password down somewhere safe. If you lose it, the VM cannot be opened.**
5. Disk: 80 GB, do not pre-allocate.
6. Customize Hardware: Memory 8192 MB; Processors 1 processor with 4 cores; Network NAT.
7. Options > Advanced: firmware UEFI with Secure Boot on (normally the default for Windows 11).

## 4. Install Windows and take the first snapshot
1. Power on; if it says "press any key to boot from DVD", press a key immediately.
2. Install Windows 11 Pro; create a local account (the throwaway one).
3. Install **VMware Tools** (VM menu) for screen size, clipboard and smooth mouse, then reboot.
4. Create a second local administrator account named `admin2` with its own throwaway password (recovery account).
5. Snapshot: VM > Snapshot > Take Snapshot, name it **`clean`**.

## 5. Install the dev tools inside the VM
1. **Visual Studio 2022 Community.** In the installer select:
   - **Desktop development with C++** (this includes MSVC and the Windows 11 SDK; confirm the SDK box is ticked).
   - Optional: **.NET desktop development** if you will write the enrollment tool in C#.
   - Optional: the Windows SDK Debugging Tools (WinDbg).
2. **Git for Windows** (includes Git Credential Manager for GitHub login).
3. Settings > Windows Update > pause updates for a few weeks, so the VM does not restart mid-test.
4. Settings > Power: set screen/sleep to never (the lock test is manual).
5. Snapshot: **`tools-ready`**.

## 6. Git and GitHub
Main PC (once), inside the unzipped project folder:
```
git init
git add .
git commit -m "initial plan"
git branch -M main
git remote add origin <your private repo URL>
git push -u origin main
```
VM (once): `git clone <your private repo URL>` (sign in through the browser prompt). After that, `git pull` before every build.
Rules: keep the repo private; never commit passwords, keys, or the VM credentials. The included `.gitignore` excludes build output and logs.

## 7. Codex on the main PC
- Open the project folder in Codex. It reads `AGENTS.md` automatically.
- **Model:** pick the newest flagship coding model your picker shows. Documentation I found lists a GPT-5.6 family (`gpt-5.6-sol` frontier, `gpt-5.6-terra` balanced, `gpt-5.6-luna` fast) and older `gpt-5.5` / `gpt-5.4`; names change often and this was not confirmed on OpenAI's own pages, so trust your picker. Use the top flagship. Avoid "mini" and "spark" models for this project.
- **Reasoning effort:**
  - medium: most work (windows, tools, scripts, installer)
  - high: planning each phase, debugging crashes, credential serialization, storage/TPM code, security review
  - xhigh: rare, only for a stubborn bug that high could not solve (uses several times more of your usage limit)
- Optional `~/.codex/config.toml` (check against your Codex version):
```
model_reasoning_effort = "medium"
plan_mode_reasoning_effort = "high"
```
- Start a fresh Codex session for each phase. `AGENTS.md`, `PLAN.md` and `docs/PROGRESS.md` carry the context between sessions.
- Paste prompts from `prompts/START_PROMPT.md` and `prompts/PHASE_PROMPTS.md`.

## 8. The loop for every phase
1. **Main PC:** Codex plans (high), you approve, Codex implements (medium) and commits.
2. **Main PC:** `git push`.
3. **VM:** take a snapshot (name it `phaseN-attemptM`), then `git pull`.
4. **VM:** build in Visual Studio (x64, Debug first), run the register script as administrator, lock, test.
5. **VM:** collect evidence (screenshot, Event Viewer errors, the provider's log file).
6. **Main PC:** paste the evidence into Codex. Codex fixes. Repeat.
7. When the phase's "Done when" checks pass: update `docs/PROGRESS.md`, commit, and take a snapshot named `phaseN-pass`.

## 9. Testing inside VMware
- **Lock the VM:** Start menu > account icon > Lock (Win+L may be caught by the host). **Ctrl+Alt+Del:** VM menu > Send Ctrl+Alt+Del.
- **Sign-in screen test:** restart the VM and check the tile there too.
- **Logs:** Event Viewer (`eventvwr.msc`) > Windows Logs > Application; look for LogonUI and Application Error entries. Also check the provider's own log file (location chosen by the code, documented in `docs/PROGRESS.md`).
- **Deployment of the DLL:** follow the official sample's instructions (the DLL is typically copied to the system folder and registered with a .reg file). Use the register/unregister scripts Codex produces.
- **Link the C/C++ runtime statically** (the /MT option) so the DLL does not need extra runtime files inside LogonUI. Ask Codex to confirm this choice in Phase 1.
- **Important limitation:** the VM gives your pattern window a *virtual mouse*, not your real trackpad. The VM proves that dragging works on the lock screen; it does NOT prove how it feels with your trackpad's acceleration and tap-and-drag. See Phase 7.

## 10. Recovery steps in the VM (when sign-in breaks)
1. **Fastest:** VM > Snapshot > revert to the last good snapshot.
2. **Safe Mode:** on the sign-in screen hold Shift and click Power > Restart, then Troubleshoot > Advanced options > Startup Settings > Restart > press 4 (Safe Mode). Run the unregister script or delete the provider key under `HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\Authentication\Credential Providers\{our GUID}`.
3. **Second admin account** `admin2`: sign in with it and run the uninstall script.
4. **Recovery Environment:** from Advanced options open Command Prompt and remove the key from the offline registry.

## 11. Troubleshooting table
| Symptom | Likely cause | What to try |
| --- | --- | --- |
| Tile never appears | DLL is 32-bit, wrong path, GUID mismatch between the two registry keys, missing dependency | Build x64; compare both registry entries; link runtime statically; check Event Viewer |
| Tile appears but selecting it crashes LogonUI | Unhandled exception in the DLL | Read the log and Application error; wrap entry points in try/catch; revert snapshot |
| Pattern window does not appear | Window created with wrong parent or on wrong thread | Check the provider's window-creation callback usage against the official sample |
| Window appears but ignores drag | Input not reaching it, or the window never gets focus | Log mouse messages (debug build, no secrets); report to Codex; see fallback in PLAN section 11 |
| Correct pattern shows success but does not unlock | Credential serialization or authentication package problem | Compare against the sample's packing code; check Event Viewer Security log for the failure reason |
| Sign-in fails after password change | Stored password is stale | Re-enroll; the provider must fall back to Password tile |
| VM very slow | RAM pressure or Hyper-V/VBS conflict | Close host tabs; check VMware's Hyper-V compatibility notes |
| VM will not open | Lost the TPM encryption password | No recovery; recreate the VM. Keep the password safe |
| Windows Update restarted the VM | Updates not paused | Pause updates; re-run the register step if needed |

## 12. Phase 7 preview: real hardware
After every VM test passes, tune the trackpad feel safely by running the pattern window as a **standalone desktop test app on your main PC** (no provider install). Only consider installing the provider on a real machine after full testing and with the safeguards in `docs/RECOVERY.md`.

## 13. Time and effort (rough, part-time)
Setup (this guide): 1 day. Phases 1-6: 6 to 9 weeks. Phase 7: a few days.

## 14. Honest limitations
- Nothing in this package has been run on a real Windows lock screen yet; the first real proof is Phase 1 and Phase 2 in your VM.
- The password-recovery design is a convenience trade-off, not stronger than a password.
- Some Windows API details must be confirmed against Microsoft's official documentation and sample during the build.
