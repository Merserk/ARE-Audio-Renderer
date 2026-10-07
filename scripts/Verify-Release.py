"""Verify release ZIPs against the staged binaries and complete source tree."""
from pathlib import Path
import argparse
import hashlib
import json
import os
import re
import struct
import zipfile

root = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--output-dir", type=Path)
parser.add_argument("--staging-dir", type=Path)
args = parser.parse_args()
version = re.search(r"project\(ASIORenderEngine VERSION (\d+\.\d+\.\d+)",
                    (root / "CMakeLists.txt").read_text(encoding="utf-8")).group(1)
release = args.output_dir or root / "dist" / version
staging = args.staging_dir or root / "out" / "package"
records = []
for line in (release / "SHA256SUMS.txt").read_text(encoding="ascii").splitlines():
    digest, name = line.split("  ", 1)
    assert Path(name).name == name and name.startswith(f"ARE-Audio-Renderer-{version}-"), name
    path = release / name
    with path.open("rb") as stream:
        assert hashlib.file_digest(stream, "sha256").hexdigest() == digest, name
    architecture = "source" if name.endswith("-source.zip") else "x64" if name.endswith("-x64.zip") else "x86" if name.endswith("-x86.zip") else None
    assert architecture, name
    prefix = f"ARE-Audio-Renderer-{version}-{architecture}/"
    with zipfile.ZipFile(path) as archive:
        assert archive.testzip() is None, name
        entries = archive.namelist()
        assert len(entries) == len(set(entries)), name
        assert all(entry.startswith(prefix) and ".." not in Path(entry).parts for entry in entries), name
        for entry in entries:
            relative = entry[len(prefix):]
            data = archive.read(entry)
            expected = (root if architecture == "source" else staging / architecture) / relative
            assert data == (os.readlink(expected).encode("utf-8") if expected.is_symlink() else expected.read_bytes()), relative
            if architecture == "source":
                assert not relative.endswith((".exe", ".dll", ".pdb", ".wav", ".mkv", ".mka", ".f32", ".pyc")), relative
                assert Path(relative).parts[0] not in ("out", ".research", "GitHub", "dist", "Video for tests"), relative
            elif relative.endswith((".exe", ".dll")):
                assert data[:2] == b"MZ", relative
                offset = struct.unpack_from("<I", data, 0x3c)[0]
                assert data[offset:offset + 4] == b"PE\0\0", relative
                assert struct.unpack_from("<H", data, offset + 4)[0] == (0x8664 if architecture == "x64" else 0x014c), relative
                assert (version + ".0").encode("utf-16-le") in data, relative
        if architecture == "source":
            required = ("CMakeLists.txt", "README.md", "CHANGELOG.md", "VALIDATION.md", "SOURCE.txt",
                        "src/win/filter.cpp", "src/win/interfaces.hpp", "src/win/property_page.cpp",
                        "tests/filter_tests.cpp", "tests/live_reconfigure_smoke.cpp",
                        "third_party/asio/common/asio.h", "third_party/r8brain/CDSPResampler.h",
                        "third_party/soxr/src/soxr.c", "third_party/soxr-build/CMakeLists.txt")
            assert all(prefix + item in entries for item in required), name
        else:
            required = ("ARE-Audio-Renderer.dll", "ARE-Audio-Renderer-Settings.exe", "ARE-Audio-Renderer-Control.exe",
                        "Install.cmd", "Install.ps1", "Uninstall.bat", "Uninstall.ps1", "README.md", "LICENSE", "THIRD_PARTY_NOTICES.md")
            assert all(prefix + item in entries for item in required), name
            assert "GNU GENERAL PUBLIC LICENSE" in archive.read(prefix + "LICENSE").decode(), name
            assert version in archive.read(prefix + "README.md").decode(), name
        records.append({"file": name, "sha256": digest, "bytes": path.stat().st_size,
                        "entries": len(entries), "checks_passed": True})
    print(f"{name}: SHA-256, CRC, contents, architecture and version verified.")
assert len(records) == 3 and {record["file"] for record in records} == {
    f"ARE-Audio-Renderer-{version}-{architecture}.zip" for architecture in ("x64", "x86", "source")}
report = {"version": version, "all_passed": True, "archives": records}
evidence = root / "tests" / "results" / version / "release-validation.json"
if evidence.is_file():
    validation = json.loads(evidence.read_text(encoding="utf-8"))
    assert validation["version"] == version and validation["all_passed"], "Release tests failed"
    report["release_validation"] = validation
(release / f"VERIFIED-{version}.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
