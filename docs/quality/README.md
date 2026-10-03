# Audio measurement method

ARE Audio Renderer 0.1.0 was compared with Windows System Default audio output
using actual DirectShow playback and WASAPI digital loopback. The main
[README](../../README.md#measured-comparison) presents the comparison table.
[results.json](results.json) contains every run, all 31 frequency measurements,
per-second music errors, capture counters and SHA-256 digests;
[results.csv](results.csv) is the flat per-run table.

## Recording and test matrix

The music source is the user's local **Michael Jackson – Thriller** FLAC:
stereo, 44.1 kHz, signed 16-bit PCM, approximately 5 minutes 57 seconds. The
**60–72 second** excerpt is used for all music cases. Its peak is scaled to
−18 dBFS before fixture quantization. The source-file digest identifies the
exact recording; it does not establish the recording's provenance or mastering.
The music, generated music fixtures and recorded audio are not redistributed.

| Signal | Input formats | Body / analyzed duration | Purpose |
| --- | --- | --- | --- |
| Thriller excerpt | 44.1 kHz PCM16; derived 48 / 96 kHz PCM24 | 12 / 11 s | Stereo waveform agreement on real music |
| 1 kHz sine, −18 dBFS peak | 44.1 / 48 / 96 kHz PCM24 | 4 / 3 s | THD+N and level accuracy |
| 31 simultaneous tones, 80 Hz–20 kHz, −18 dBFS combined peak | 44.1 / 48 / 96 kHz PCM24 | 4 / 3 s | Frequency-dependent gain error |
| 1 kHz sine, −90 dBFS peak | 96 kHz PCM24 | 4 / 3 s | Low-level quantization and path noise |
| 1 kHz + 29 kHz, each −24 dBFS peak | 96 kHz PCM24 | 4 / 3 s | Residual at the potential 19 kHz alias frequency |
| Left: 1 kHz; right: 1.7 kHz, −18 dBFS peak | 48 kHz PCM24 | 4 / 3 s | Interchannel leakage |
| Digital silence | 48 kHz PCM24 | 4 / 3 s | Settled nonzero samples |

This gives **13 fixtures × 3 paths × 3 repetitions = 117 captures**. Repetitions
are interleaved in the fixed order System Default → r8brain → SoX Sinc. Each
process uses a fresh graph and a one-second gap separates captures. Derived
48/96 kHz music is the same 44.1 kHz recording converted numerically; it is not
native high-resolution source material.

## Playback and capture configuration

Measurement graphs connect each renderer directly to decoded WAV playback.
They omit MPC-HC's Audio Switcher and host processing. MPC-HC playback requires
the [documented host compatibility](../../integration/mpc-hc/README.md).

| Setting | Published configuration |
| --- | --- |
| Renderer DLL | ARE Audio Renderer 0.1.0.0, x64; exact binary SHA-256 in results |
| Operating system | Windows build 26300; complete platform string in results |
| Baseline | `CLSID_DSoundRender`, the Windows DirectSound baseline for System Default output |
| ARE paths | r8brain and SoX Sinc; requested renderer connection verified |
| ASIO driver | FL Studio ASIO, `{188135E1-7171-3434-854F-01A3C71F3DF9}` |
| Actual ASIO rate | 44.1 kHz |
| Keep device sample rate | Enabled explicitly for measurement; enabled by default for new preferences |
| Renderer gain / precision | Unity / Automatic |
| Smooth transitions | Disabled to avoid deliberate transition processing |
| Routing | First two driver output channels; stereo |
| Driver buffer | 512 frames, about 11.61 ms at 44.1 kHz |
| Driver-reported output latency | 2,898 frames, about 65.71 ms at 44.1 kHz; not measured analog latency |
| Capture | Default console render endpoint, stereo 48 kHz, Float32 loopback |
| Pre-test validation | One second of loopback before graph Run; any nonzero pre-roll rejects the capture |
| Completion | DirectShow `EC_COMPLETE`, followed by 500 ms of capture drain |
| Analysis software | NumPy, SciPy and SoundFile; exact versions in results |

Both paths reach the same endpoint, but their processing stages differ. The
baseline sends DirectSound audio to the 48 kHz Windows path. ARE sends audio at
44.1 kHz to FL Studio ASIO, whose bridge reaches that Windows endpoint. The
bridge's conversion is included. At 44.1 kHz input, ARE's selected converter is
bypassed because input and ASIO rates match. The two ARE algorithms should then
produce the same settled music samples; that case tests the output path rather
than comparing the converters.

The baseline is explicitly pinned to DirectSound rather than selected by filter
merit. MPC-HC's System Default selection leaves renderer choice to graph
building, so a customized filter installation can choose a different renderer.
The comparison applies to the stated DirectSound baseline. See the pinned
[MPC-HC graph builder](https://github.com/clsid2/mpc-hc/blob/a84d0cf38a1866f3518bb819c901300dacff5a9b/src/mpc-hc/FGManager.cpp)
and [Output selection code](https://github.com/clsid2/mpc-hc/blob/a84d0cf38a1866f3518bb819c901300dacff5a9b/src/mpc-hc/PPageOutput.cpp).

The harness does not change the default device, endpoint mix rate, mixer volume
or enhancement settings. It loads the DLL directly without changing COM
registration. Renderer preference names, types, values and absent values are
saved and restored for every ARE graph. Do not edit renderer settings during
an active capture.

## Fixture generation and alignment

Each WAV contains a 0.3-second 200–8,000 Hz chirp marker, 0.2 seconds of silence,
a 0.75-second 1 kHz calibration tone at −18 dBFS, another 0.2 seconds of silence,
the signal body, and 0.2 seconds of trailing silence. Marker and signal edges
use 20 ms fades. Analysis discards 0.5 seconds from each end of the body.

The reference is the **quantized PCM actually written to the WAV**, converted
to 48 kHz with SciPy's Fourier resampler and one second of zero padding on each
side. An unchanged 48 kHz fixture requires no reference resampling. This is a
specified numerical reference, not a claim about an ideal analog waveform.

Chirp cross-correlation gives the integer origin. A single fractional-sample
delay, bounded to ±1.1 endpoint samples, and one common gain for both channels
are fitted for waveform error. No channel-specific equalization, time warping,
noise removal or dynamic gain correction is applied. Frequency measurements
use the separately measured 1 kHz calibration gain. Music retains its original
stereo content; synthetic mono tests use identical left and right samples.

| Metric | Definition and interpretation |
| --- | --- |
| Music waveform SNR, dB | `20 log10(RMS(gain × reference) / RMS(capture − gain × reference))`. Higher means closer agreement with the specified reference after alignment and gain fitting. It is not a listening score or the recording's signal-to-noise ratio. |
| Sample-exact flag | Strict equality of captured and reference stereo samples at the integer alignment, before fractional delay or gain fitting. Stored per run. |
| Per-second music SNR | The same fitted gain/delay, evaluated over each of the eleven analyzed one-second blocks. Stored in JSON to expose localized errors. |
| THD+N, dBc | A least-squares 1 kHz sine/cosine and DC fit is removed; residual RMS is divided by fitted fundamental RMS. No A-weighting or conventional analog analyzer bandwidth is applied. Source quantization, conversion, driver and Windows-path errors are included. |
| Multitone gain deviation, dB | A three-second periodic Hann FFT measures each of 31 integer-frequency tones. The captured/reference amplitude ratio is normalized by the calibration gain. The main table gives the maximum absolute deviation. All signed frequency errors and the 20 kHz value are retained. |
| 19 / 15.1 kHz residual, dBc | Three-second Hann periodogram bin power relative to the 1 kHz bin. Unfiltered 29 kHz can fold to 19 kHz at 48 kHz or to 15.1 kHz at 44.1 kHz. Both bins and total residual are reported because the paths have different intermediate rates. Noise and other errors also contribute; these values are not isolated stopband measurements. |
| Channel leakage, dBc | Unwanted-tone power / wanted-tone power in each channel; the worse result is reported. Values below −180 dBc are displayed as below the reporting floor. |
| Silence | Number of nonzero samples in both settled channels. An exactly zero result is distinguished from a finite noise floor. |
| Clipping | Number of analyzed stereo samples with absolute amplitude at least 1.0; peak levels are also retained. |
| Capture integrity | Raw/metadata frame agreement, finite samples, sufficient marker/body coverage, pre-roll peak, WASAPI discontinuity and timestamp flags. |
| ARE delivery | Actual ASIO rate, delivered frames, buffer and latency, underrun and overload counters. System Default has no equivalent ARE telemetry; those fields are null/blank rather than claimed zero. |

Zero captured peak/RMS and undefined silence SNR are stored as null; linear peak/RMS fields distinguish exact zero from a finite value. Tiny nonzero silence tails are shown below the −180 dBFS reporting floor rather than presented as analog noise measurements.

The main table reports each case's median and full minimum–maximum range, not
only its best run. SNR above 150 dB is shown as an **analysis floor** rather than
as measured analog performance. An exact 48 kHz digital pass-through may reach
that limit. The numerical values remain available in the per-run data. Narrow-bin residual and channel-leakage values below −180 dBc are displayed below the reporting floor, not as hundreds of decibels of measured analog rejection.

The final numerical analysis was regenerated from the saved captures after adding both alias-frequency bins and explicit zero/floor handling. No additional music playback was needed. The result identifies the capture orchestration script separately from the current analysis script; fixture, binary and capture hashes remain unchanged.

## What the comparison establishes

These measurements describe this recording, these synthetic signals and this
Windows/ASIO setup. They can identify digital errors and frequency differences
in the captured paths. They do not measure a DAC, analog noise, jitter,
headphones, speakers or audible preference. A native ASIO device may bypass the
Windows endpoint entirely and cannot be evaluated by this loopback method.

A different endpoint rate, driver, output precision, mixer gain or Windows
effect can change the results. Source-rate matching can favor System Default
because it avoids a conversion; a rate mismatch can favor a different path.
No universal claim that ARE is always cleaner follows from this experiment.

Setup captures made before the comparison were excluded because another audio
stream was present, or because they used the earlier music source. Published
runs identify their own source and fixture hashes. A failed pre-roll is a
validity failure, not a measurement of renderer quality. Other observed errors
and repeated-run variation are retained rather than discarded for producing a
less favorable result.

## Reproduce

Regenerate the README table and CSV from the published numerical data without
playing audio:

```powershell
python -m pip install numpy scipy soundfile
python scripts/Measure-Quality.py
```

Run the actual capture matrix using your own lossless stereo file. Keep other
audio paused and choose an ASIO driver that bridges to the default **stereo
48 kHz** Windows endpoint. These commands **play music and test tones**:

```powershell
cmake --preset windows-x64 -DARE_BUILD_MEASUREMENTS=ON
cmake --build --preset release-x64
python scripts/Measure-Quality.py --capture `
  --music "C:\Michael Jackson - Thriller.flac" `
  --music-title "Michael Jackson – Thriller" --music-start 60 `
  --build-dir out/x64 `
  --driver "{188135E1-7171-3434-854F-01A3C71F3DF9}" `
  --driver-name "FL Studio ASIO" --repetitions 3 `
  --work-dir out/quality-thriller
```

`--only music_44100 --repetitions 1` makes a three-capture setup check. Use a
fresh work folder for a new recording. `--resume` reuses captures only when
fixture, renderer, capture tool, mode and driver fingerprints match. Raw Float32
recordings, WAV fixtures and checkpoints stay in the private work folder.
The music file is required to rerun the waveform experiment; the included
numerical results alone cannot reconstruct the recordings.

Live capture replaces the published result files and measured table. Use
`--results out/my-results.json --readme out/my-readme.md` to preserve them; copy
the main README to that alternate path first because the script updates its
measurement markers. No screenshots, figures or image-generation libraries
are needed.




## Detailed numerical checks

The following tables summarize the same captured runs. All repetitions are retained in the JSON/CSV; these tables show medians and observed ranges.

<details>
<summary>Thriller: per-second waveform agreement, 44.1 kHz input</summary>

Units: dB, higher is closer to the numerical reference; values above 150 dB are at the analysis floor. A single gain and delay are fitted across the entire eleven-second window. Each row is a one-second portion of that window.

| Track interval | System Default (DirectSound) | ARE / r8brain | ARE / SoX Sinc |
| --- | ---: | ---: | ---: |
| 60.5–61.5 s | 57.73 | 64.19 | 64.19 |
| 61.5–62.5 s | 52.11 | 58.45 | 58.45 |
| 62.5–63.5 s | 56.20 | 64.30 | 64.30 |
| 63.5–64.5 s | 54.36 | 61.78 | 61.78 |
| 64.5–65.5 s | 59.60 | 70.67 | 70.67 |
| 65.5–66.5 s | 52.52 | 60.60 | 60.60 |
| 66.5–67.5 s | 55.36 | 61.92 | 61.92 |
| 67.5–68.5 s | 53.91 | 61.59 | 61.59 |
| 68.5–69.5 s | 57.90 | 65.74 | 65.74 |
| 69.5–70.5 s | 52.83 | 60.57 | 60.57 |
| 70.5–71.5 s | 56.85 | 62.99 | 62.99 |

</details>

<details>
<summary>Thriller: per-second waveform agreement, 48 kHz input</summary>

Units: dB, higher is closer to the numerical reference; values above 150 dB are at the analysis floor. A single gain and delay are fitted across the entire eleven-second window. Each row is a one-second portion of that window.

| Track interval | System Default (DirectSound) | ARE / r8brain | ARE / SoX Sinc |
| --- | ---: | ---: | ---: |
| 60.5–61.5 s | ≥150 (analysis floor) | 62.88 | 63.61 |
| 61.5–62.5 s | ≥150 (analysis floor) | 57.08 | 58.20 |
| 62.5–63.5 s | ≥150 (analysis floor) | 63.21 | 64.20 |
| 63.5–64.5 s | ≥150 (analysis floor) | 61.04 | 61.70 |
| 64.5–65.5 s | ≥150 (analysis floor) | 70.14 | 70.56 |
| 65.5–66.5 s | ≥150 (analysis floor) | 59.48 | 60.50 |
| 66.5–67.5 s | ≥150 (analysis floor) | 60.86 | 61.61 |
| 67.5–68.5 s | ≥150 (analysis floor) | 60.33 | 61.14 |
| 68.5–69.5 s | ≥150 (analysis floor) | 64.82 | 65.60 |
| 69.5–70.5 s | ≥150 (analysis floor) | 59.94 | 60.48 |
| 70.5–71.5 s | ≥150 (analysis floor) | 61.81 | 62.84 |

</details>

<details>
<summary>Thriller: per-second waveform agreement, 96 kHz input</summary>

Units: dB, higher is closer to the numerical reference; values above 150 dB are at the analysis floor. A single gain and delay are fitted across the entire eleven-second window. Each row is a one-second portion of that window.

| Track interval | System Default (DirectSound) | ARE / r8brain | ARE / SoX Sinc |
| --- | ---: | ---: | ---: |
| 60.5–61.5 s | 87.41 | 62.88 | 63.61 |
| 61.5–62.5 s | 81.63 | 57.08 | 58.20 |
| 62.5–63.5 s | 87.98 | 63.21 | 64.20 |
| 63.5–64.5 s | 86.19 | 61.04 | 61.70 |
| 64.5–65.5 s | 96.30 | 70.14 | 70.56 |
| 65.5–66.5 s | 84.37 | 59.48 | 60.50 |
| 66.5–67.5 s | 85.80 | 60.86 | 61.61 |
| 67.5–68.5 s | 85.23 | 60.33 | 61.14 |
| 68.5–69.5 s | 89.94 | 64.82 | 65.60 |
| 69.5–70.5 s | 85.64 | 59.94 | 60.48 |
| 70.5–71.5 s | 86.48 | 61.81 | 62.84 |

</details>

<details>
<summary>Frequency-dependent gain: all 31 tones, 44.1 kHz input</summary>

Units: signed dB error relative to the decoded fixture / Fourier reference, normalized to each capture’s 1 kHz calibration. Positive means more amplitude; negative means less. Six decimal places aid numerical comparison; they do not express analog accuracy.

| Frequency | System Default (DirectSound) | ARE / r8brain | ARE / SoX Sinc |
| --- | ---: | ---: | ---: |
| 80 Hz | -0.000021 | -0.000000 | -0.000000 |
| 96 Hz | -0.000020 | 0.000000 | 0.000000 |
| 116 Hz | -0.000020 | -0.000000 | -0.000000 |
| 139 Hz | -0.000019 | -0.000000 | -0.000000 |
| 167 Hz | -0.000017 | 0.000000 | 0.000000 |
| 201 Hz | -0.000015 | -0.000000 | -0.000000 |
| 241 Hz | -0.000013 | -0.000000 | -0.000000 |
| 290 Hz | -0.000009 | -0.000000 | -0.000000 |
| 349 Hz | -0.000004 | -0.000000 | -0.000000 |
| 419 Hz | 0.000001 | 0.000000 | 0.000000 |
| 504 Hz | 0.000007 | 0.000000 | 0.000000 |
| 606 Hz | 0.000012 | 0.000000 | 0.000000 |
| 728 Hz | 0.000014 | 0.000000 | 0.000000 |
| 875 Hz | 0.000009 | 0.000000 | 0.000000 |
| 1052 Hz | -0.000004 | 0.000000 | 0.000000 |
| 1265 Hz | -0.000019 | -0.000000 | -0.000000 |
| 1521 Hz | -0.000020 | -0.000000 | -0.000000 |
| 1828 Hz | 0.000002 | 0.000000 | 0.000000 |
| 2197 Hz | 0.000012 | 0.000000 | 0.000000 |
| 2641 Hz | -0.000018 | -0.000000 | -0.000000 |
| 3175 Hz | -0.000003 | 0.000000 | 0.000000 |
| 3816 Hz | -0.000002 | -0.000000 | -0.000000 |
| 4588 Hz | -0.000003 | 0.000000 | 0.000000 |
| 5515 Hz | -0.000024 | -0.000000 | -0.000000 |
| 6629 Hz | -0.000005 | -0.000000 | -0.000000 |
| 7968 Hz | -0.000001 | -0.000000 | -0.000000 |
| 9579 Hz | -0.000023 | -0.000001 | -0.000001 |
| 11514 Hz | -0.000014 | 0.000001 | 0.000001 |
| 13841 Hz | -0.000045 | -0.000005 | -0.000005 |
| 16638 Hz | -0.000053 | -0.000037 | -0.000037 |
| 20000 Hz | -0.257126 | 0.007389 | 0.007389 |

</details>

<details>
<summary>Frequency-dependent gain: all 31 tones, 48 kHz input</summary>

Units: signed dB error relative to the decoded fixture / Fourier reference, normalized to each capture’s 1 kHz calibration. Positive means more amplitude; negative means less. Six decimal places aid numerical comparison; they do not express analog accuracy.

| Frequency | System Default (DirectSound) | ARE / r8brain | ARE / SoX Sinc |
| --- | ---: | ---: | ---: |
| 80 Hz | 0.000000 | -0.000000 | -0.000000 |
| 96 Hz | 0.000000 | -0.000000 | -0.000000 |
| 116 Hz | 0.000000 | -0.000000 | -0.000000 |
| 139 Hz | 0.000000 | -0.000000 | -0.000000 |
| 167 Hz | 0.000000 | -0.000000 | -0.000000 |
| 201 Hz | 0.000000 | -0.000000 | -0.000000 |
| 241 Hz | 0.000000 | -0.000000 | -0.000000 |
| 290 Hz | 0.000000 | -0.000000 | -0.000000 |
| 349 Hz | 0.000000 | -0.000000 | -0.000000 |
| 419 Hz | 0.000000 | 0.000000 | -0.000000 |
| 504 Hz | 0.000000 | 0.000000 | 0.000000 |
| 606 Hz | 0.000000 | -0.000000 | 0.000000 |
| 728 Hz | 0.000000 | 0.000000 | 0.000000 |
| 875 Hz | 0.000000 | 0.000000 | 0.000000 |
| 1052 Hz | 0.000000 | -0.000000 | -0.000000 |
| 1265 Hz | 0.000000 | -0.000000 | -0.000000 |
| 1521 Hz | 0.000000 | -0.000000 | -0.000000 |
| 1828 Hz | 0.000000 | 0.000000 | 0.000000 |
| 2197 Hz | 0.000000 | 0.000000 | 0.000000 |
| 2641 Hz | 0.000000 | -0.000000 | -0.000000 |
| 3175 Hz | 0.000000 | 0.000000 | 0.000000 |
| 3816 Hz | 0.000000 | -0.000000 | -0.000000 |
| 4588 Hz | 0.000000 | 0.000000 | 0.000000 |
| 5515 Hz | 0.000000 | -0.000000 | -0.000000 |
| 6629 Hz | 0.000000 | -0.000000 | -0.000000 |
| 7968 Hz | 0.000000 | -0.000000 | -0.000000 |
| 9579 Hz | 0.000000 | -0.000001 | -0.000001 |
| 11514 Hz | 0.000000 | 0.000001 | 0.000001 |
| 13841 Hz | 0.000000 | -0.000005 | -0.000005 |
| 16638 Hz | 0.000000 | -0.000037 | -0.000037 |
| 20000 Hz | 0.000000 | 0.007389 | 0.007389 |

</details>

<details>
<summary>Frequency-dependent gain: all 31 tones, 96 kHz input</summary>

Units: signed dB error relative to the decoded fixture / Fourier reference, normalized to each capture’s 1 kHz calibration. Positive means more amplitude; negative means less. Six decimal places aid numerical comparison; they do not express analog accuracy.

| Frequency | System Default (DirectSound) | ARE / r8brain | ARE / SoX Sinc |
| --- | ---: | ---: | ---: |
| 80 Hz | -0.000023 | -0.000000 | -0.000000 |
| 96 Hz | -0.000022 | -0.000000 | -0.000000 |
| 116 Hz | -0.000022 | -0.000000 | -0.000000 |
| 139 Hz | -0.000021 | -0.000000 | -0.000000 |
| 167 Hz | -0.000020 | -0.000000 | -0.000000 |
| 201 Hz | -0.000019 | -0.000000 | -0.000000 |
| 241 Hz | -0.000017 | -0.000000 | -0.000000 |
| 290 Hz | -0.000014 | -0.000000 | -0.000000 |
| 349 Hz | -0.000010 | -0.000000 | -0.000000 |
| 419 Hz | -0.000006 | -0.000000 | -0.000000 |
| 504 Hz | -0.000001 | 0.000000 | -0.000000 |
| 606 Hz | 0.000004 | 0.000000 | -0.000000 |
| 728 Hz | 0.000007 | -0.000000 | 0.000000 |
| 875 Hz | 0.000005 | -0.000000 | -0.000000 |
| 1052 Hz | -0.000003 | -0.000000 | -0.000000 |
| 1265 Hz | -0.000016 | -0.000000 | -0.000000 |
| 1521 Hz | -0.000024 | -0.000000 | -0.000000 |
| 1828 Hz | -0.000014 | 0.000000 | -0.000000 |
| 2197 Hz | 0.000005 | 0.000000 | 0.000000 |
| 2641 Hz | -0.000008 | -0.000000 | -0.000000 |
| 3175 Hz | -0.000023 | -0.000000 | 0.000000 |
| 3816 Hz | 0.000006 | -0.000000 | -0.000000 |
| 4588 Hz | -0.000026 | 0.000000 | 0.000000 |
| 5515 Hz | -0.000000 | -0.000000 | -0.000000 |
| 6629 Hz | -0.000008 | -0.000000 | -0.000000 |
| 7968 Hz | -0.000023 | -0.000000 | -0.000000 |
| 9579 Hz | -0.000018 | -0.000001 | -0.000001 |
| 11514 Hz | -0.000008 | 0.000001 | 0.000001 |
| 13841 Hz | -0.000044 | -0.000005 | -0.000005 |
| 16638 Hz | -0.000067 | -0.000037 | -0.000037 |
| 20000 Hz | -0.000018 | 0.007389 | 0.007389 |

</details>



