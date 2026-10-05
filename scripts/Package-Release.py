from pathlib import Path
import argparse
import hashlib
import os
import re
import stat
import zipfile

root = Path(__file__).resolve().parents[1]
arguments = argparse.ArgumentParser(description="Package and verify ARE release archives.")
arguments.add_argument("--replace", action="store_true", help="Atomically replace existing release archives after CRC verification.")
arguments.add_argument("--architecture", choices=("x64", "x86", "both"), default="both")
arguments.add_argument("--staging-dir", type=Path, help="Directory containing x64/x86 install staging folders.")
arguments.add_argument("--output-dir", type=Path, help="Destination for the two release ZIPs.")
arguments.add_argument("--include-source", action="store_true", help="Also archive the complete source and vendored dependencies.")
options = arguments.parse_args()
version_match = re.search(r"project\(ASIORenderEngine VERSION (\d+\.\d+\.\d+)",
                          (root / "CMakeLists.txt").read_text(encoding="utf-8"))
if not version_match:
    raise RuntimeError("Cannot read project version from CMakeLists.txt")
version = version_match.group(1)
staging = options.staging_dir or root / "out" / "package"
release = options.output_dir or root / "dist" / version
release.mkdir(parents=True, exist_ok=True)

def archive(name, files, extra=None):
    destination = release / name
    if destination.exists() and not options.replace:
        raise RuntimeError(f"Archive already exists: {destination}")
    temporary = destination.with_name(destination.name + ".tmp")
    if temporary.exists():
        raise RuntimeError(f"Unfinished archive already exists: {temporary}")
    if not destination.resolve().is_relative_to(release.resolve()) or not temporary.resolve().is_relative_to(release.resolve()):
        raise RuntimeError("Archive path escaped the release directory")
    with zipfile.ZipFile(temporary, "w", zipfile.ZIP_DEFLATED, compresslevel=6) as output:
        for source, relative in files:
            if source.is_symlink():
                entry = zipfile.ZipInfo(relative)
                entry.create_system = 3
                entry.external_attr = (stat.S_IFLNK | 0o777) << 16
                output.writestr(entry, os.readlink(source).encode("utf-8"))
            else:
                output.write(source, relative)
        for relative, data in (extra or {}).items():
            output.writestr(relative, data)
    with zipfile.ZipFile(temporary) as check:
        bad = check.testzip()
        if bad:
            raise RuntimeError(f"Archive CRC failed: {bad}")
    temporary.replace(destination)
    print(f"Created and verified {destination.name}: {destination.stat().st_size:,} bytes", flush=True)

for arch in (("x64", "x86") if options.architecture == "both" else (options.architecture,)):
    package = staging / arch
    if not (package / "ARE-Audio-Renderer.dll").is_file():
        raise RuntimeError(f"Missing staged renderer: {package}")
    names = ("ARE-Audio-Renderer.dll", "ARE-Audio-Renderer-Settings.exe",
             "ARE-Audio-Renderer-Control.exe", "Install.cmd", "Install.ps1",
             "Uninstall.ps1", "Uninstall.bat", "README.md", "LICENSE", "THIRD_PARTY_NOTICES.md")
    sources = [package / name for name in names]
    sources.extend(sorted((package / "licenses").glob("*")))
    if any(not p.is_file() for p in sources) or not (package / "licenses").is_dir():
        raise RuntimeError(f"Incomplete release staging: {package}")
    files = [(p, f"ARE-Audio-Renderer-{version}-{arch}/" + p.relative_to(package).as_posix())
             for p in sources]
    archive(f"ARE-Audio-Renderer-{version}-{arch}.zip", files)

if options.include_source:
    names = ("CMakeLists.txt", "CMakePresets.json", "README.md", "CHANGELOG.md",
             "VALIDATION.md", "SOURCE.txt", "LICENSE", "THIRD_PARTY_NOTICES.md",
             ".gitignore", ".editorconfig")
    sources = [root / name for name in names if (root / name).is_file()]
    for directory in ("src", "resources", "scripts", "tests", "third_party", "integration", "docs", ".github"):
        sources.extend(sorted(p for p in (root / directory).rglob("*")
                              if p.is_file() and "__pycache__" not in p.parts and p.suffix != ".pyc"))
    files = [(p, f"ARE-Audio-Renderer-{version}-source/" + p.relative_to(root).as_posix()) for p in sources]
    archive(f"ARE-Audio-Renderer-{version}-source.zip", files)

checksums = []
for path in sorted(release.glob(f"ARE-Audio-Renderer-{version}-*.zip")):
    with path.open("rb") as source:
        digest = hashlib.file_digest(source, "sha256").hexdigest()
    checksums.append(f"{digest}  {path.name}\n")
temporary = release / "SHA256SUMS.txt.tmp"
temporary.write_text("".join(checksums), encoding="ascii")
temporary.replace(release / "SHA256SUMS.txt")
print("Created SHA256SUMS.txt", flush=True)

