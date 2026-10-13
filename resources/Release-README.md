# ARE Audio Renderer 3.1.1

Use the x64 package for a 64-bit player or x86 for a 32-bit player.

1. Extract the ZIP to a folder and run Install.cmd.
2. Restart your player and select ARE Audio Renderer as its audio renderer.
3. Select your ASIO device in ARE-Audio-Renderer-Settings.exe.

Version 3.1.1 fixes Install.cmd/Uninstall.bat under Windows PowerShell.
Reinstall reuses identical loaded files and installs changed files separately,
so an open player or settings window does not cause a DLL overwrite error.
Restart MPC-HC and close older settings windows to load the updated renderer.

Click the gear next to ARE Audio Renderer in MPC-HC's Options > Playback >
Output. Version 3.1.0 restores this button after official MPC-HC updates without
replacing the player executable or requiring a specific MPC-HC version.
The helper starts at installation and Windows sign-in. It runs silently as
ARE-Audio-Renderer-Settings.exe --mpc-integration, independently for each
installed architecture. Allow about one second after opening Output or
selecting ARE for the gear to activate. Run the player normally, without
administrator elevation, so the per-user helper can reach its controls.

The gear opens the active renderer for live status and Apply during playback;
before playback it opens a configuration instance. Compatibility uses MPC-HC's
standard Windows combobox and adjacent gear layout. Future UI redesigns may
require an integration update; there is no version-number lock.

To install the renderer without the helper, use Install.ps1 -NoMpcIntegration.
Settings remain available through Play > Filters > ARE Audio Renderer, or the
standalone settings tool. MPC-HC updates do not remove the ARE helper.

Keep device sample rate (resample audio) is enabled by default for new settings.
Existing saved preferences are preserved. Windows audio sharing depends on the ASIO driver.

Playback-rate changes retain the ASIO stream
through MPC-HC's brief graph restart. Click Apply in the active renderer's
properties page to activate changed SRC, precision or device settings. Playback
restarts at the current position and retains speed, volume, mute and paused state.
The standalone settings tool configures the next opened renderer instance.

Mono uses both stereo outputs.
Surround is preserved when the ASIO device has enough outputs; otherwise ARE
mixes it to stereo or mono, including dialogue, surround speakers and LFE with
clipping headroom. The live properties page shows the input/output mapping.

WAV, AIFF, ALAC, MP3, AAC, AC3, E-AC3 (Dolby Digital Plus), TrueHD, Opus,
Vorbis, FLAC, DTS, WavPack and WMA are supported through the player's PCM decoder.
Disable compressed bitstream output in LAV Audio when using ARE. Atmos-tagged
audio plays its decoded channel bed; encoded/object passthrough is unsupported.

Run Uninstall.bat to unregister the renderer, stop its helper and remove its
Windows startup entry. Close your player
before removing its files. Saved device preferences are retained.

ARE-Audio-Renderer-Control.exe provides registration, device listing, and diagnostics;
run it without arguments to display usage. MPC-HC is obtained separately.

Copyright (C) Merserk. See LICENSE, THIRD_PARTY_NOTICES.md, and licenses/.
