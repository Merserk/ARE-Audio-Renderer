# ARE Audio Renderer

[![Downloads](https://img.shields.io/github/downloads/Merserk/ARE-Audio-Renderer/total.svg?style=flat-square&label=Downloads&color=brightgreen)](https://github.com/Merserk/ARE-Audio-Renderer/releases) ![Platform](https://img.shields.io/badge/Platform-Windows-0078D4?style=flat-square&logo=windows11&logoColor=white) ![Architecture](https://img.shields.io/badge/Architecture-x64%20%7C%20x86-6f42c1?style=flat-square) [![Host](https://img.shields.io/badge/Host-MPC--HC-5C6BC0?style=flat-square)](https://github.com/clsid2/mpc-hc) ![Audio](https://img.shields.io/badge/Audio-ASIO-8A2BE2?style=flat-square) ![SRC](https://img.shields.io/badge/SRC-r8brain%20%7C%20SoX%20Sinc-57A64A?style=flat-square) [![Patreon](https://img.shields.io/badge/Patreon-Merserk-FF424D?style=flat-square&logo=patreon&logoColor=white)](https://www.patreon.com/Merserk)

**ASIO audio output for [MPC-HC](https://github.com/clsid2/mpc-hc), with precise, configurable sample rate conversion.**


<p align="center">
  <img
    src="https://github.com/user-attachments/assets/de47b93f-ca7a-4ec1-a43d-76e171193f1c"
    alt="image"
    style="width: 450px; max-width: 100%; height: auto;"
  />
</p>

ARE Audio Renderer connects decoded PCM audio to your ASIO device through a
DirectShow renderer. Choose r8brain or SoX conversion, control the output format,
and inspect the active audio path from the properties page.

- **Keep the device sample rate.** Resampling is enabled by default for new settings; existing preferences are preserved.
- **Choose the conversion engine.** r8brain and SoX Sinc both process audio in Float64 with linear phase filters.
- **Inspect playback.** See input/output formats, sample rates, buffer latency, conversion status and underrun counters.
- **Control the output.** Select an ASIO device, first output channel, buffer size and automatic or explicit PCM precision.

## Download and install

| MPC-HC architecture | Release package |
| --- | --- |
| 64-bit | [ARE-Audio-Renderer-0.1.0-x64.zip](https://github.com/Merserk/ARE-Audio-Renderer/releases/download/v0.1.0/ARE-Audio-Renderer-0.1.0-x64.zip) |
| 32-bit | [ARE-Audio-Renderer-0.1.0-x86.zip](https://github.com/Merserk/ARE-Audio-Renderer/releases/download/v0.1.0/ARE-Audio-Renderer-0.1.0-x86.zip) |

Use the package matching **MPC-HC**, and install an ASIO driver of the same
architecture. Obtain MPC-HC separately from its [official releases](https://github.com/clsid2/mpc-hc/releases).

The validated host is **MPC-HC 2.8.2 with the included compatibility patch**.
Its stock Audio Switcher rejects ARE during connection; the patch also enables
the Output settings button for external renderers. Follow the
[host build instructions](integration/mpc-hc/README.md) before installation.

1. Close MPC-HC, extract the ZIP and run **Install.cmd**. The installer opens the renderer settings after registration.
2. Choose your **ASIO device** and apply the settings.
3. Restart MPC-HC. In **Options → Playback → Output → Audio Renderer**, select **ARE Audio Renderer**.

Open the settings during playback through MPC-HC's **Play → Filters → ARE Audio
Renderer** entry, or use **ARE-Audio-Renderer-Settings.exe**. Windows audio sharing
depends on the ASIO driver. For unregistration, run **Uninstall.bat** from the
installed or extracted folder.

## Audio processing

| Component | Configuration in this release |
| --- | --- |
| [r8brain-free-src 7.6](https://github.com/avaneev/r8brain-free-src) | Default converter; Float64, linear phase, 0.5% transition band, 218 dB stopband design target |
| [libsoxr 0.1.3](https://github.com/chirlu/soxr) | Optional Sinc converter; Float64, linear phase, 33-bit design precision, passband to 99.8% of Nyquist |
| Output conversion | Driver-native format in Automatic mode; explicit 16 / 24 / 32-bit PCM choices; TPDF dither when integer quantization is required |
| Processing model | Conversion on the producer thread, bounded audio queue, lightweight ASIO buffer callback |

The filter design values are configuration targets, rather than measurements of
the complete device path. Resampling, gain changes and precision reduction alter
the samples. A lossless path is available when source/device rates and formats are
compatible, playback is at normal speed and unity volume, and transition processing
is disabled. ARE renders decoded PCM; compressed bitstream passthrough is unsupported.

## Measured comparison

<!-- measurement:start -->
**117 live captures** · 13 signals / formats · three output paths · 3 repetitions per case.

Music: **Michael Jackson – Thriller**, 60–72 s from a local lossless stereo file. The same excerpt was tested as 44.1 kHz PCM16 and derived 48/96 kHz PCM24. No music or captured audio is included in the release.

Both actual DirectShow renderers were captured digitally at the same **48 kHz Windows endpoint**. ARE used **FL Studio ASIO** at **44.1 kHz**, with Keep device sample rate enabled, unity renderer volume, automatic output precision and transition smoothing disabled. System/device settings were unchanged between runs. Captures with audio present before the test started are rejected.

Values are medians; small text gives the minimum…maximum across repeated runs. ↑ means higher is better; ↓ means lower is better.

| Input / measurement | Unit | System Default (DirectSound) | ARE / r8brain | ARE / SoX Sinc |
| --- | --- | ---: | ---: | ---: |
| Music, aligned waveform SNR, 44.1 kHz / 16-bit | dB ↑ | 55.07<br><sub>55.07…55.07</sub> | 62.34<br><sub>62.34…62.34</sub> | 62.34<br><sub>62.34…62.34</sub> |
| Music, aligned waveform SNR, 48 kHz / 24-bit | dB ↑ | ≥150<br><sub>analysis floor</sub> | 61.25<br><sub>61.25…61.25</sub> | 62.12<br><sub>62.12…62.12</sub> |
| Music, aligned waveform SNR, 96 kHz / 24-bit | dB ↑ | 86.15<br><sub>86.15…86.15</sub> | 61.25<br><sub>61.25…61.25</sub> | 62.12<br><sub>62.12…62.12</sub> |
| 1 kHz, THD+N, 44.1 kHz / 24-bit | dBc ↓ | -118.83<br><sub>-118.83…-118.83</sub> | -111.99<br><sub>-111.99…-111.99</sub> | -111.99<br><sub>-111.99…-111.99</sub> |
| 1 kHz, THD+N, 48 kHz / 24-bit | dBc ↓ | -127.30<br><sub>-127.30…-127.30</sub> | -112.06<br><sub>-112.06…-112.06</sub> | -112.06<br><sub>-112.06…-112.06</sub> |
| 1 kHz, THD+N, 96 kHz / 24-bit | dBc ↓ | -131.49<br><sub>-131.49…-131.49</sub> | -112.14<br><sub>-112.14…-112.14</sub> | -112.14<br><sub>-112.14…-112.14</sub> |
| 31 tones, worst gain deviation, 44.1 kHz / 24-bit | dB ↓ | 0.2571<br><sub>0.2571…0.2571</sub> | 0.0074<br><sub>0.0074…0.0074</sub> | 0.0074<br><sub>0.0074…0.0074</sub> |
| 31 tones, worst gain deviation, 48 kHz / 24-bit | dB ↓ | 0.0000<br><sub>0.0000…0.0000</sub> | 0.0074<br><sub>0.0074…0.0074</sub> | 0.0074<br><sub>0.0074…0.0074</sub> |
| 31 tones, worst gain deviation, 96 kHz / 24-bit | dB ↓ | 0.0001<br><sub>0.0001…0.0001</sub> | 0.0074<br><sub>0.0074…0.0074</sub> | 0.0074<br><sub>0.0074…0.0074</sub> |
| −90 dBFS 1 kHz, THD+N, 96 kHz / 24-bit | dBc ↓ | -60.12<br><sub>-60.12…-60.12</sub> | -61.16<br><sub>-61.16…-61.16</sub> | -61.16<br><sub>-61.16…-61.16</sub> |
| 1 + 29 kHz, 19 kHz residual, 96 kHz / 24-bit | dBc ↓ | -112.32<br><sub>-112.32…-112.32</sub> | -135.91<br><sub>-135.91…-135.91</sub> | -135.91<br><sub>-135.91…-135.91</sub> |
| 1 + 29 kHz, 15.1 kHz residual, 96 kHz / 24-bit | dBc ↓ | <−180<br><sub>reporting floor</sub> | -164.45<br><sub>-164.45…-164.45</sub> | -164.45<br><sub>-164.45…-164.45</sub> |
| 1 + 29 kHz, total residual, 96 kHz / 24-bit | dBc ↓ | -112.24<br><sub>-112.24…-112.24</sub> | -112.09<br><sub>-112.09…-112.09</sub> | -112.09<br><sub>-112.09…-112.09</sub> |
| L: 1 kHz / R: 1.7 kHz, channel leakage, 48 kHz / 24-bit | dBc ↓ | <−180 | -161.20<br><sub>-161.20…-161.20</sub> | -161.20<br><sub>-161.20…-161.20</sub> |
| Digital silence, nonzero samples, 48 kHz / 24-bit | samples ↓ | 0 | 0 | 12754 |
| Digital silence, settled RMS level | dBFS ↓ | digital zero | digital zero | <−180<br><sub>numerical tail</sub> |

| Reliability check | Observed result |
| --- | ---: |
| Completed captures | 117/117 |
| Captured samples at or above digital full scale | 0 |
| ARE-reported underruns / overloads | 0 / 0 |
| Loopback discontinuity flags, excluding first packet | 0 |
| Loopback timestamp error flags | 0 |
| Pre-test audio / rejected captures | 0 |

Music SNR is measured against a zero-padded Fourier reference after fitting one shared stereo gain and one fractional delay. It is waveform agreement, not a listening score. THD+N includes source quantization, SRC, driver and Windows-path errors. The two paths include different driver resampling stages; these results do not isolate the converter or measure DAC/analog performance.

[Full method, per-run measurements and reproduction](docs/quality/README.md).
<!-- measurement:end -->
For the original **44.1 kHz music**, ARE's captured path showed **7.27 dB higher
waveform agreement** and less multitone gain variation. DirectSound was closer
to the reference for the derived **48/96 kHz** music, including sample-exact
48 kHz playback. The 19 kHz ultrasonic residual favored ARE, while total
residual slightly favored DirectSound. At 44.1 kHz input the ARE converter is
bypassed; the ASIO driver and Windows bridge are part of these measured results.

## Build from source

Install **Visual Studio 2026** with Desktop development with C++, a Windows SDK,
**[CMake 4.2+](https://cmake.org/cmake/help/latest/generator/Visual%20Studio%2018%202026.html)**
for the supplied presets, and **Python 3** for packaging. All required DSP sources
and ASIO interface headers are included.

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/Build.ps1
```

The script builds and tests both architectures, then writes the two release ZIPs
to `dist/0.1.0/`. Use `-Architecture x64` or `-Architecture x86` for one build.
Release binaries use the static MSVC runtime. Both architectures passed all seven
CTest checks covering PCM precision, conversion, playback rate, transitions,
the ASIO engine and the DirectShow filter contract.

The `tests/` folder is retained because these checks protect audio integrity and
player integration. Test executables are excluded from release packages. The
live measurement tool is optional (`-DARE_BUILD_MEASUREMENTS=ON`); its method and
reproduction commands are in [the measurement documentation](docs/quality/README.md).

## License

Original renderer code is **[MIT licensed](LICENSE)**, copyright © 2026 Merserk.
Third-party sources retain their own licenses. The combined Windows binaries
are distributed under **GPLv3** because the included Steinberg ASIO SDK headers
use their GPL option; r8brain is MIT and libsoxr is LGPL-2.1-or-later.
Complete source and build scripts are provided for rebuilding and relinking.
The separate MPC-HC compatibility patch is GPL-3.0-or-later.
See [third-party notices](THIRD_PARTY_NOTICES.md) for the pinned revisions and license texts.


