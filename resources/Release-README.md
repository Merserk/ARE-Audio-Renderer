# ARE Audio Renderer 0.3.2

1. Extract the ZIP matching your player: x64 for mpc-hc64.exe, x86 for mpc-hc.exe.
2. Close MPC-HC and run Install.cmd. Select your ASIO device in the settings.
3. Restart MPC-HC and open a file. Use the gear in Options > Playback > Output
to view the active ASIO driver, formats, buffer and delivered frame count.

The installer selects ARE and disables MPC-HC's internal Audio Switcher in
detected profiles. The switcher rejects ARE before connecting audio and causes
Windows audio fallback. The direct path is decoder -> ARE -> selected ASIO
driver. ARE supplies volume, speed, resampling and channel conversion. Internal
Audio Switcher normalization, boost and time shift are unavailable with it disabled.

For a portable player, run Install.ps1 -MpcHcPath 'C:\Portable\MPC-HC\mpc-hc64.exe'.
To preserve player settings, use -NoMpcConfiguration and manually select ARE
and disable Internal Filters > Audio Switcher > Enable built-in audio switcher
filter. -NoMpcIntegration omits only the settings-button helper.

ARE properties follow the player's actual dark/light dialog colours. Standalone
settings use MPC-HC's saved MPCTheme preference. During playback the gear opens
the active renderer; standalone settings configure the next opened instance.
Apply in the active page restarts audio while preserving position and paused state.

Version 0.3.2 fixes the Options crash caused by the integration hook's large
stack frame. The gear uses native button clicks and avoids recursive enable
updates during the player's idle processing.

Install.cmd and Uninstall.bat work with Windows PowerShell. Identical loaded
files are reused; changed binaries are staged elsewhere, avoiding DLL overwrite
errors. Close old players and settings windows to load an update.

Uninstall.bat unregisters ARE and removes its helper startup entry. When the
last architecture is removed, it restores backed-up player settings without
overwriting a later renderer choice. ASIO device preferences are retained.

Disable compressed bitstream output in your decoder; ARE receives decoded PCM.
Windows audio sharing depends on the ASIO driver. MPC-HC is obtained separately.

Copyright (C) Merserk. See LICENSE, THIRD_PARTY_NOTICES.md and licenses/.
