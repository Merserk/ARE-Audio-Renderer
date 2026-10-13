# ARE Audio Renderer 3.1.1 checks

Validation date: 2026-10-13 (Europe/Kiev).

All nine CTest suites pass on x64 and Win32, including the changed integration
fixture that launches the helper with a quoted flag and trailing whitespace.
See ctest-x64.txt and ctest-x86.txt. The audio engine is unchanged.

installer_tests.ps1 executes Install.cmd and Uninstall.bat through CMD, which
starts Windows PowerShell 5.1. It tests flag forwarding and exit status; reads
helper process metadata and the architecture-specific mutex to confirm a
hidden helper rather than an accidental settings dialog; and verifies that
reinstall/uninstall waits for the old helper process to exit.

Installed DLLs are held open with FileShare.Read to deny writes. Identical
reinstall, uninstall followed by reinstall, and another valid binary with the
same file version all succeed. The latter uses a harmless PE overlay and must
leave the locked original intact, register a separate payload, and reuse that
payload on subsequent identical installs. Installed-folder uninstall is tested
while the DLL remains locked. See installer-x64.txt and installer-x86.txt.

The installer fixture uses temporary LOCALAPPDATA folders, serializes with
other architecture-specific validation, and restores prior per-user renderer
registration/startup values. It leaves saved audio preferences intact. A local
check also successfully reinstalled the user's extracted 3.1.0 package while
the reported old process continued to hold its identical installed DLL.

---

# ARE Audio Renderer 3.1.0 checks

Validation date: 2026-10-13 (Europe/Kiev).

Clean source builds: Visual Studio 18 2026, MSVC 19.51.36260.0, SDK 10.0.26100.0,
Release configuration, C++23, x64 and Win32. The release DLL, settings helper
and control tool were built from the source prepared for the GitHub update.

Each architecture passes all nine CTest suites. See ctest-x64.txt and
ctest-x86.txt. The new integration fixture uses actual Windows controls,
a thread-specific hook, COM property frames, and a DirectShow graph; it
checks the displayed status to distinguish a configuration instance from
an active graph instance. It simulates repeated idle disable updates between
mouse-down and mouse-up, and exercises BM_CLICK and keyboard activation.
It also checks selection changes, stale-instance removal, helper shutdown,
restoring the original disabled state and refusing ambiguous button layouts.

The installed official MPC-HC 2.8.3 was inspected and its ARE gear became
enabled. Desktop automation could not complete its click because it reported
the owned modal control as outside/mismatched with the main target window;
modal click behavior is verified by the native integration fixtures.

Earlier physical audio/codec measurements remain in ../0.3.0/. The 3.1.0
release changes settings integration and renderer-instance discovery.

---

# ARE Audio Renderer 0.3.0 validation

Validation date: 2026-10-07 (Europe/Kiev).

## Rate changes

DirectShow stops and resumes its filters around SetRate, including MPC-HC's
Decrease Rate, Increase Rate and Reset Rate controls. Version 0.2.0 drained,
stopped, disposed and reinitialized ASIO on each cycle. Version 0.3.0 resets
queued/converter history and retains a silent stream for 250 ms. Pause/Run
cancels the release deadline. Sustained Stop, disconnect and filter destruction
release the device; pending configuration changes force an immediate close.

The simulated-driver regression checks both converters with 44.1/48 kHz input
and a 48 kHz device. Running decrease/increase/reset must not initialize,
start, stop, dispose or change the ASIO hardware rate. Existing signal tests
check speed/pitch, anti-aliasing, fractional rates, duration, tail draining and
exact transport after restoring 1x playback.

Physical tests use FL Studio ASIO, its retained 44.1 kHz clock, 512-frame
buffers and generated 48 kHz stereo PCM. Each converter exercises 0.8x, 1.2x
and 1.0x twice. Renderer gain is -30 dB on a quiet tone. The 0.2.0 x64 baseline
measured 261–314 ms for SetRate. Current per-architecture timings and individual
measurements are in tests/results/0.3.0/hardware-rate-summary.json and
hardware-rate-*.txt. These runs reported no underruns or driver overloads.

| Run | SetRate range | SetRate median | Delivery resumes |
| --- | ---: | ---: | ---: |
| 0.2.0 x64 baseline | 261.25–313.96 ms | 284.52 ms | 294.75–360.17 ms |
| 0.3.0 x64 | 2.12–9.24 ms | 4.39 ms | 7.52–68.58 ms |
| 0.3.0 x86 | 2.12–17.64 ms | 9.17 ms | 8.53–72.13 ms |

API time ends when SetRate returns. Delivery time ends when the renderer has
delivered at least one ASIO buffer after the segment reset. Neither includes
the complete MPC-HC UI interaction or downstream physical DAC latency. The
reported approximately 600 ms delay was not reproduced on this driver; its
reinitialization overhead was reproduced and removed. Other drivers can have
different timings.

## Apply during playback

The property page saves changed values and calls a separate live Apply COM
interface once. Existing layouts, CLSIDs and registry keys remain compatible.
The graph stops, seeks to its captured position and returns to its previous
running/paused state with the new device/processing settings. Apply waits for
state restoration, including asynchronous Run.

The actual property-page regression checks:

- SRC, PCM precision, output routing and buffer changes activate together in
  one ASIO reopen, with no pending settings after Apply.
- Speed, volume, mute and running/paused state are preserved; position stays
  within the tested bounds and audio delivery resumes.
- Identical Apply keeps the stream open without a restart.
- An unavailable output channel returns an error, preserves page edits and
  restores the previous stream/configuration. Successful recovery does not
  post a fatal graph event to the player.
- Sustained Stop releases the retained device within the test deadline.
- Twenty-four synchronized Run/NewSegment and Run/flush races leave the
  consumer running; repeated graph tests check the formerly intermittent stall.

Physical x64/x86 tests also switch SRC/PCM precision while playing, switch SRC
while paused, verify position/state and resume audio on the same graph. Normal
file graphs must support absolute seeking for live Apply; unsupported graphs
return an Apply error. The standalone helper configures a separate instance
for subsequent playback. Replacing a loaded DLL still requires a player restart.

## Build, decoding and output checks

Eight CTest suites cover channel mapping, PCM/queues, volume/transitions,
speed/pitch, both SRC backends including scalar processing, ASIO lifecycle and
the DirectShow/property-page contract. Both architectures are independently
built and tested from GitHub/Main. Integration programs serialize temporary
preferences and simulated-driver registration within each architecture.

The LAV matrix uses 69 generated codec/layout fixtures plus five eight-second
supplied-video audio excerpts. Each architecture checks 8-, 2- and 1-output
ASIO at 48 kHz with r8brain, and stereo at 44.1 kHz with Sinc: 552 codec cases
plus 40 video cases. Current mappings, frame counts, peaks and underruns are
in tests/results/0.3.0/. The aggregate release-validation.json records completed
checks and totals. Actual LAV Splitter/Audio Decoder 0.83.0.5-git is used with
mixing, layout expansion and compressed bitstreaming disabled at runtime.

Previous channel validation and eight physical captures remain in
[the 0.2.0 validation](docs/validation/0.2.0.md) and tests/results/0.2.0/.
The historical 117-capture comparison in docs/quality is from 0.1.0 and was
not relabeled as a new comparison. New 0.3.0 loopback checks are listed
separately in the current release validation.

## Release artifacts and reproduction

Version 0.3.0.0 is embedded in the x64/x86 renderer, settings and control
binaries. Binary/source archives are prepared locally in GitHub/Releases;
no remote release is published by this work. Packages are staged from the
independent GitHub/Main builds. Packaging checks ZIP CRCs and writes SHA256SUMS.
Verify-Release.py checks hashes, complete file equality, duplicate/path safety,
PE architecture/version and exclusion of private media/build data from source.
The archives retain original-source and third-party license texts.

Both architectures were installed per user, verified as registered/loadable,
and compared byte-for-byte with the tested staging DLLs. Installation preserved
the exact renderer preferences. Restart MPC-HC once to load the installed DLL.

```powershell
.\scripts\Build.ps1
.\scripts\Test-Codecs.ps1 -Architecture both -LavDirectory C:\LAV `
  -FixturesDirectory out/codec-fixtures -IncludeSinc
# Optional physical test with an installed matching ASIO driver CLSID:
.\out\x64\Release\live_reconfigure_smoke.exe `
  .\out\x64\Release\ARE-Audio-Renderer.dll '<ASIO-CLSID>'
python scripts/Package-Release.py --replace --include-source
python scripts/Verify-Release.py
```

Use matching x86 test binaries and LAV runtimes for a 32-bit graph. Videos,
generated/captured audio, LAV binaries and build output are excluded from
release source. Tests and installation preserve renderer preferences,
including registry value types and absent values.
