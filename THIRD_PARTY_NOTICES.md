# Third-party notices

ARE Audio Renderer source authored by Merserk is under the MIT license
(`LICENSE` in the source checkout; `licenses/ARE-MIT.txt` in a binary package).
Vendored code keeps its own license. The combined Windows release uses Steinberg
ASIO headers under their GPLv3 option and is distributed under GPL-3.0; MIT
permissions on the original renderer source remain available. Complete source
and build scripts are in this repository.

The paths in the table refer to the source checkout. Binary release packages
include the third-party license texts and copyright notices in `licenses/`.

| Component | Source revision | License |
| --- | --- | --- |
| Steinberg ASIO SDK 2.3.4 interface headers | audiosdk/asio `496a0765b8bb9c26f764f22f9a9712a937177db2` | GPLv3 option; see `third_party/asio/LICENSE.txt` and `third_party/ASIO-GPL-3.0.txt` |
| r8brain-free-src 7.6, Aleksey Vaneev / Voxengo | `cb2abb9977efe2471979b380ed95daa56ab4fdb9` | MIT; `third_party/r8brain/LICENSE` |
| libsoxr 0.1.3, Rob Sykes | `945b592b70470e29f917f4de89b4281fbbd540c0` | LGPL-2.1-or-later; `third_party/soxr/COPYING.LGPL` and `LICENCE` |
| Ooura FFT / PFFFT / FFTPACK | Upstream sources retained above | Notices in `third_party/r8brain/OOURA-FFT-NOTICE.txt` and `third_party/soxr-build/FFT-NOTICES.txt` |
| MPC-HC compatibility patch | MPC-HC 2.8.2, `a84d0cf38a1866f3518bb819c901300dacff5a9b` | GPL-3.0-or-later; `integration/mpc-hc/README.md`; full GPL text in `third_party/ASIO-GPL-3.0.txt` |

The current CMake adapter builds libsoxr without modifying its DSP sources.
Source for the statically linked library and the renderer is included so users
can rebuild and relink with a modified libsoxr. Third-party copyright notices
are retained in source and release packages. ASIO is a trademark of Steinberg
Media Technologies GmbH. MPC-HC is a separate GPLv3 application and is not bundled.

Upstream: [ASIO headers](https://github.com/audiosdk/asio),
[r8brain](https://github.com/avaneev/r8brain-free-src),
[libsoxr](https://github.com/chirlu/soxr),
[Steinberg licensing](https://www.steinberg.net/developers/).
