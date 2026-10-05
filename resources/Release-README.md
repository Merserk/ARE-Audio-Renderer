# ARE Audio Renderer 0.2.0

Use the x64 package for a 64-bit player or x86 for a 32-bit player.

1. Extract the ZIP to a folder and run Install.cmd.
2. Restart your player and select ARE Audio Renderer as its audio renderer.
3. Select your ASIO device in ARE-Audio-Renderer-Settings.exe.

Keep device sample rate (resample audio) is enabled by default for new settings.
Existing saved preferences are preserved. Windows audio sharing depends on the ASIO driver.

Version 0.2.0 adds automatic channel handling. Mono uses both stereo outputs.
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

Validated host: MPC-HC 2.8.2 with the compatibility patch documented at
https://github.com/Merserk/ARE-Audio-Renderer/tree/main/integration/mpc-hc
The stock 2.8.2 Audio Switcher rejects ARE during pin connection.

Copyright (C) Merserk. See LICENSE, THIRD_PARTY_NOTICES.md, and licenses/.

Original renderer source: MIT (licenses/ARE-MIT.txt). This combined binary
distribution: GPLv3, including the ASIO SDK under its GPL option. Complete source
and build instructions: https://github.com/Merserk/ARE-Audio-Renderer
