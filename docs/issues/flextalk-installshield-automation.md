# Cannot automate Watson/FlexTalk InstallShield installer

## Summary
Automating the Watson/FlexTalk InstallShield 5 `setup.exe` continues to fail in scripted Wine installs. The wizard still requires manual interaction even when started through helper scripts, blocking fully unattended prefix creation.

## Environment
- Host: Linux with Wine 8+
- Target prefix: 32-bit (Windows XP compatibility)
- Tooling: winetricks-based setup (`scripts/winetricks/setup_nettts_prefix.sh`)
- Installer: Watson/FlexTalk voice package (InstallShield 5, circa 1997)

## Reproduction steps
1. Start from a clean 32-bit Wine prefix configured with `winxp` in winetricks.
2. Run `scripts/winetricks/setup_nettts_prefix.sh` (or the upstream raw version) to bootstrap the prefix and download dependencies.
3. During the FlexTalk stage, the script launches `setup.exe` for the voice package.
4. The InstallShield GUI appears and waits for user input; attempts to auto-advance the dialogs via flags or silent switches have no effect.

## Expected result
The FlexTalk installer should accept a silent/automated invocation (e.g., response file, `/s` flag, or extracted CABs) so the Wine prefix setup can finish unattended.

## Actual result
- No documented silent flags accepted; `/s`, `/sms`, `/r`, and `/f1` all appear to be ignored.
- The installer window must be driven manually; the helper script waits indefinitely until the user completes the wizard.
- Extracting files from the InstallShield cabinet does not produce a straightforward manual install (missing registry entries and control panel integration).

## Notes and prior attempts
- Tried recording a response file with `/r` and replaying with `/s /f1setup.iss`; the installer still shows the GUI and ignores the script.
- `setup.log` does not appear after runs, suggesting the response file mechanism may be disabled in this package.
- Launching via `wine start /wait setup.exe`, `wineconsole`, or `env __COMPAT_LAYER=RunAsInvoker` does not change behavior.
- Running the installer under Windows 10 x64 and Windows XP VMs shows the same interactivity requirements.
- Manual installation succeeds; automation remains the blocker for a fully unattended nettts prefix.

## Impact
The winetricks helper (`scripts/winetricks/setup_nettts_prefix.sh`) cannot complete without user interaction, limiting CI automation and unattended deployments.
