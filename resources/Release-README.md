# ARE Audio Renderer 0.3.0

Use the x64 package for a 64-bit player or x86 for a 32-bit player.

1. Extract the ZIP to a folder and run Install.cmd.
2. Restart your player and select ARE Audio Renderer as its audio renderer.
3. Select your ASIO device in ARE-Audio-Renderer-Settings.exe.

Keep device sample rate (resample audio) is enabled by default for new settings.
Existing saved preferences are preserved. Windows audio sharing depends on the ASIO driver.

Version 0.3.0 makes playback-rate changes faster by retaining the ASIO stream
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

Run Uninstall.bat to unregister the renderer. Close your player
before removing its files. Saved device preferences are retained.

ARE-Audio-Renderer-Control.exe provides registration, device listing, and diagnostics;
run it without arguments to display usage. MPC-HC is obtained separately.

Copyright (C) Merserk. See LICENSE, THIRD_PARTY_NOTICES.md, and licenses/.
