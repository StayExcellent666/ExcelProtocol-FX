"""Dependency-free release validation and packaging helpers. No credentials needed."""
from __future__ import annotations

import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path, PurePosixPath
import re
import struct
import subprocess
import sys
import zipfile

VERSION = re.compile(r"^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)(?:-beta\.([1-9][0-9]*))?$")
REPOSITORY = re.compile(r"^[A-Za-z0-9_.-]+/ExcelProtocol-FX$")
DLL = "obs-plugins/64bit/excelprotocol-fx.dll"
DATA = "data/obs-plugins/excelprotocol-fx/"
SOUNDS = tuple(DATA + "sounds/" + name + ".wav" for name in
               ("raid-start", "raid-shot-blaster-1", "raid-shot-blaster-2",
                "raid-shot-plasma-1", "raid-shot-plasma-2",
                "raid-shot-railgun-1", "raid-shot-railgun-2",
                "raid-shot-hammer-1", "raid-shot-hammer-2",
                "raid-enemy-hit-1", "raid-enemy-hit-2", "raid-enemy-hit-3",
                "raid-enemy-death-1", "raid-enemy-death-2", "raid-enemy-death-3",
                "raid-enemy-spawn-1", "raid-enemy-spawn-2", "raid-alert", "raid-unlock",
                "raid-fire", "raid-ice", "raid-victory", "raid-failure"))
REQUIRED = (DLL, DATA + "excelprotocol-fx-updater.exe", DATA + "locale/en-US.ini",
            DATA + "tls/qschannelbackend.dll", *SOUNDS)
QA_CHECKS = ("native_plugin_load", "desktop_and_obs_effects", "raid_battle_gameplay_and_audio", "settings_persistence", "websocket_authentication", "installer_clean_install", "installer_upgrade_rollback", "installer_obs_running_refusal", "uninstall_preserves_obs", "stable_beta_update_selection", "update_digest_rejection", "update_after_obs_exit", "performance_and_panic_stop")


def version_info(value: str) -> tuple[str, str]:
    value = value.removeprefix("v")
    if not VERSION.fullmatch(value):
        raise ValueError("Version must be X.Y.Z or X.Y.Z-beta.N (N >= 1)")
    return value, "beta" if "-beta." in value else "stable"


def sha256(path: Path) -> str:
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def check_pe(path: Path, x64: bool = True) -> None:
    with path.open("rb") as stream:
        header = stream.read(64)
        if len(header) < 64 or header[:2] != b"MZ":
            raise ValueError(f"Not a PE executable: {path}")
        offset = struct.unpack_from("<I", header, 0x3C)[0]
        if not 64 <= offset <= path.stat().st_size - 6:
            raise ValueError(f"Invalid PE offset: {path}")
        stream.seek(offset)
        pe = stream.read(6)
        if pe[:4] != b"PE\0\0" or (x64 and pe[4:] != b"\x64\x86"):
            raise ValueError(f"Expected Windows x64 PE binary: {path}")


def safe_payload_name(name: str) -> bool:
    parts = PurePosixPath(name).parts
    return (not name.startswith("/") and "\\" not in name and ":" not in name
            and not any(p in (".", "..") for p in parts)
            and (name == DLL or name.startswith(DATA)))


def stage_files(stage: Path) -> list[Path]:
    stage = stage.resolve(strict=True)
    files = []
    for path in stage.rglob("*"):
        if path.is_symlink() or (hasattr(path, "is_junction") and path.is_junction()):
            raise ValueError(f"Reparse points are not allowed in the install payload: {path}")
        if path.is_file():
            relative = path.relative_to(stage).as_posix()
            if not safe_payload_name(relative):
                raise ValueError(f"Unexpected installed file: {relative}")
            files.append(path)
    names = {p.relative_to(stage).as_posix() for p in files}
    for name in REQUIRED:
        if name not in names:
            raise ValueError(f"Missing required installed file: {name}")
    for path in files:
        if path.suffix.lower() in (".dll", ".exe"):
            check_pe(path)
        elif path.suffix.lower() == ".wav":
            header = path.read_bytes()[:12]
            if len(header) != 12 or header[:4] != b"RIFF" or header[8:] != b"WAVE":
                raise ValueError(f"Invalid WAV audio asset: {path}")
    if len({str(p.relative_to(stage)).casefold() for p in files}) != len(files):
        raise ValueError("Case-insensitive duplicate installation paths")
    return sorted(files)


def make_payload(stage: Path, output: Path) -> None:
    stage = stage.resolve(strict=True)
    files = stage_files(stage)
    output.mkdir(parents=True, exist_ok=True)
    # The installer backs up only these exact paths; it never deletes a tree.
    names = [p.relative_to(stage).as_posix() for p in files]
    (output / "payload-files.txt").write_text("\n".join(names) + "\n", encoding="utf-8")
    manifest = {name: sha256(stage / name) for name in names}
    (output / "payload-sha256.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")


def package(stage: Path, output: Path, version: str, repository: str, commit: str) -> dict:
    stage = stage.resolve(strict=True)
    version, channel = version_info(version)
    if not REPOSITORY.fullmatch(repository):
        raise ValueError("Repository must be the official OWNER/ExcelProtocol-FX slug")
    if not re.fullmatch(r"[0-9a-f]{40}", commit):
        raise ValueError("A full lowercase source commit SHA is required")
    files = stage_files(stage)
    output.mkdir(parents=True, exist_ok=True)
    stem = f"ExcelProtocol-FX-{version}-windows-x64"
    installer = output / f"{stem}-setup.exe"
    check_pe(installer, x64=False)  # Inno bootstrap may be x86; payload is x64.
    portable = output / f"{stem}-portable.zip"
    with zipfile.ZipFile(portable, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
        for path in files:
            archive.write(path, path.relative_to(stage).as_posix())
    release_url = f"https://github.com/{repository}/releases/tag/v{version}"
    assets = {}
    for role, path in (("installer", installer), ("portable", portable)):
        assets[role] = {"name": path.name, "url": f"https://github.com/{repository}/releases/download/v{version}/{path.name}", "sha256": sha256(path), "size": path.stat().st_size}
    manifest = {"schema_version": 1, "version": version, "tag": "v" + version, "channel": channel,
                "repository": repository, "published_at": datetime.now(timezone.utc).isoformat(timespec="seconds").replace("+00:00", "Z"),
                "release_url": release_url, "commit": commit, "platform": "windows-x64",
                "obs": {"minimum": "32.2.2", "maximum_exclusive": "33.0.0"}, "assets": assets}
    (output / "update-manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    checksummed = [installer, portable, output / "update-manifest.json"]
    (output / "SHA256SUMS.txt").write_text("".join(f"{sha256(p)}  {p.name}\n" for p in checksummed), encoding="ascii")
    validate_bundle(output, version, repository, commit)
    return manifest


def validate_bundle(output: Path, version: str, repository: str, commit: str) -> None:
    version, channel = version_info(version)
    manifest = json.loads((output / "update-manifest.json").read_text(encoding="utf-8"))
    expected = {"schema_version": 1, "version": version, "tag": "v" + version, "channel": channel,
                "repository": repository, "commit": commit, "platform": "windows-x64",
                "release_url": f"https://github.com/{repository}/releases/tag/v{version}",
                "obs": {"minimum": "32.2.2", "maximum_exclusive": "33.0.0"}}
    for key, value in expected.items():
        if manifest.get(key) != value:
            raise ValueError(f"Manifest {key} does not match release")
    digest_names = set()
    for line in (output / "SHA256SUMS.txt").read_text(encoding="ascii").splitlines():
        digest, name = line.split("  ", 1)
        if Path(name).name != name or not re.fullmatch(r"[a-f0-9]{64}", digest) or sha256(output / name) != digest:
            raise ValueError(f"Invalid checksum: {name}")
        digest_names.add(name)
    for role, suffix in (("installer", "setup.exe"), ("portable", "portable.zip")):
        entry = manifest["assets"][role]
        name = f"ExcelProtocol-FX-{version}-windows-x64-{suffix}"
        if entry != {"name": name, "url": f"https://github.com/{repository}/releases/download/v{version}/{name}",
                     "sha256": sha256(output / name), "size": (output / name).stat().st_size}:
            raise ValueError(f"Invalid {role} metadata")
        if name not in digest_names:
            raise ValueError(f"Checksum missing for {name}")
    if digest_names != {a["name"] for a in manifest["assets"].values()} | {"update-manifest.json"}:
        raise ValueError("Unexpected or missing checksum entries")
    check_pe(output / manifest["assets"]["installer"]["name"], x64=False)
    with zipfile.ZipFile(output / manifest["assets"]["portable"]["name"]) as archive:
        names = archive.namelist()
        if archive.testzip() or len(set(n.casefold() for n in names)) != len(names):
            raise ValueError("Corrupt ZIP or duplicate file paths")
        if not all(safe_payload_name(name) for name in names) or not set(REQUIRED).issubset(names):
            raise ValueError("Invalid portable layout")


def validate_qa(path: Path, version: str, source: Path) -> None:
    version, channel = version_info(version)
    if channel != "stable":
        return
    qa = json.loads(path.read_text(encoding="utf-8"))
    if qa.get("version") != version or not qa.get("approved") or not qa.get("tester", "").strip():
        raise ValueError("Stable publication requires an approved, named QA report for this version")
    tested_commit = qa.get("tested_commit", "")
    if not re.fullmatch(r"[a-f0-9]{40}", tested_commit):
        raise ValueError("QA must name a full tested source commit")
    subprocess.run(["git", "merge-base", "--is-ancestor", tested_commit, "HEAD"], cwd=source, check=True)
    changed = subprocess.check_output(["git", "diff", "--name-only", tested_commit, "HEAD"], cwd=source, text=True).splitlines()
    # Only QA evidence and prose can follow the tested commit. A code/build change invalidates QA.
    if any(not (p.startswith("docs/") or p in ("README.md", "CHANGELOG.md")) for p in changed):
        raise ValueError("Source/build changes after tested_commit invalidate stable QA")
    checks = qa.get("checks", {})
    missing = [name for name in QA_CHECKS if not isinstance(checks.get(name), dict)
               or checks[name].get("result") != "pass" or not checks[name].get("evidence", "").strip()]
    if missing:
        raise ValueError("Missing passing QA evidence: " + ", ".join(missing))
    tested_at = datetime.fromisoformat(qa.get("tested_at", "").replace("Z", "+00:00"))
    if tested_at.tzinfo is None or tested_at > datetime.now(timezone.utc):
        raise ValueError("QA tested_at must be a real past UTC timestamp")
    if qa.get("obs_version") != "32.2.2" or not qa.get("windows_version"):
        raise ValueError("QA must identify tested OBS 32.2.2 and Windows versions")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("command", choices=("payload", "package", "verify", "qa", "version"))
    parser.add_argument("--stage", type=Path, default=Path("stage"))
    parser.add_argument("--output", type=Path, default=Path("dist"))
    parser.add_argument("--version", default="1.0.0")
    parser.add_argument("--repository", default="")
    parser.add_argument("--commit", default="")
    parser.add_argument("--qa", type=Path)
    parser.add_argument("--source", type=Path, default=Path.cwd())
    args = parser.parse_args()
    if args.command == "payload":
        make_payload(args.stage, args.output)
    elif args.command == "package":
        package(args.stage, args.output, args.version, args.repository, args.commit)
    elif args.command == "verify":
        validate_bundle(args.output, args.version, args.repository, args.commit)
    elif args.command == "qa":
        validate_qa(args.qa or Path(f"docs/qa/v{args.version}.json"), args.version, args.source)
    else:
        print(json.dumps(dict(zip(("version", "channel"), version_info(args.version)))))
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (ValueError, OSError, subprocess.CalledProcessError, KeyError) as error:
        print(f"Release validation failed: {error}", file=sys.stderr)
        raise SystemExit(1)
