# Changelog

## 3.1.1 — 2026-10-13

- Parse the settings helper's Windows command line with CommandLineToArgvW.
  Windows PowerShell 5.1 could append a space to --mpc-integration, causing
  3.1.0 to open a settings dialog instead of starting its hidden helper.
- Reuse byte-identical installed files without writing to loaded DLLs or EXEs.
  Changed binaries with the same version get a separate payload directory,
  allowing repair/reinstall while older player/settings instances are open.
- Wait for hidden helper processes to exit after signalling shutdown; a released
  mutex alone does not guarantee Windows has unmapped the executable/DLL.
- Keep open player/settings sessions and saved preferences during uninstall.
  Explain that loaded files remain until those windows close.
- Forward CMD/BAT arguments and preserve their PowerShell exit codes. Keep the
  install console open on failure so its error can be read.
- Add real Windows PowerShell/CMD installer regressions for locked DLLs,
  same-version changed payloads, uninstall/reinstall, helper startup and shutdown.

## 3.1.0 — 2026-10-13

- Restored the ARE settings gear in official MPC-HC after player updates. A
  per-user helper connects the existing gear to ARE's COM property page using
  Windows controls, without replacing MPC-HC or checking its release version.
- Open the active graph renderer during playback, or a configuration instance
  before playback. Live status and Apply retain their existing behavior.
- Preserve mouse clicks through MPC-HC's repeated idle button-disable updates;
  support keyboard and accessibility clicks as well.
- Start the helper at installation and Windows sign-in, independently for x64
  and x86. Uninstall stops it and removes its startup entry. Device preferences
  are retained. `Install.ps1 -NoMpcIntegration` installs only the renderer.
- Add x64/x86 integration regressions for discovery, a mouse press interrupted
  by disable updates, property pages, active/stale graph instances, renderer
  changes, shutdown and ambiguous layouts. Future player UI redesigns still
  require validation; the renderer has no player-version whitelist.


## 0.3.0 — 2026-10-07

- Removed ASIO driver teardown/reinitialization from MPC-HC's brief Stop/Run
  cycle during Decrease Rate, Increase Rate and Reset Rate. The silent stream
  is retained for 250 ms; sustained Stop, disconnect and filter destruction
  still release the device. SRC quality and pitch behavior are unchanged.
- Made the properties page's Apply button activate changed SRC, output precision,
  device, output channel, buffer size and device-rate settings through one graph
  restart at the current position. Running/paused state, speed, volume and mute
  are preserved; restarting MPC-HC is no longer needed for these changes.
- Preserved the existing COM ABIs and added a separate live Apply interface.
- Fixed a Run/NewSegment/flush race that could leave a running graph with
  audio consumption paused, especially when reusing a warm ASIO stream.
- Added rollback to the previous stream when an incompatible device/output
  configuration fails to reopen, without posting a fatal graph error after
  successful recovery. Unchanged Apply does not restart the driver.
- Added property-page regression checks and an opt-in physical ASIO rate/Apply
  benchmark, including concurrent segment/flush transitions. Test programs
  serialize temporary preferences and driver registration by architecture.
  Updated x64/x86 binaries, source package and validation evidence.

## 0.2.0 — 2026-10-06

- Fixed 5.1/7.1 playback failing on ASIO devices with fewer outputs than the
  decoded source. ARE now mixes to stereo, or mono for a single output.
- Fixed mono playing through only one side of a stereo device. Both outputs
  receive the mono signal, with exact transport when the format permits it.
- Preserved native multichannel playback when enough outputs are available.
- Added speaker-mask-aware Float64 mixing, including center dialogue,
  rear/side/height speakers and reduced-level LFE, with common clipping headroom.
- Added live input/output channel mappings and explicit downmix status through
  a separate COM interface, preserving earlier settings/status layouts.
- Accepted valid legacy 16-byte PCM media headers.
- Added channel regression tests and 69 LAV codec fixtures covering mono,
  stereo, quad, 5.1 and 7.1 in the formats listed in README.md.

See VALIDATION.md for completed release checks and their limits.

## 0.1.0

Initial ARE Audio Renderer release under its current name, with ASIO transport,
r8brain/SoX resampling, PCM precision controls, playback speed, volume,
transition smoothing and MPC-HC integration.
