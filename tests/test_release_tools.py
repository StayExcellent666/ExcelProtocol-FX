"""Release tests use synthetic PE headers as fixtures; they do not certify native binaries."""
from pathlib import Path
import importlib.util
import json
import os
import struct
import subprocess
import tempfile
import unittest

SPEC = importlib.util.spec_from_file_location("release_tools", Path(__file__).parents[1] / "scripts/release_tools.py")
release = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(release)


def pe(path, machine=0x8664):
    data = bytearray(128)
    data[:2] = b"MZ"
    struct.pack_into("<I", data, 60, 64)
    data[64:68] = b"PE\0\0"
    struct.pack_into("<H", data, 68, machine)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data)


class ReleaseTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="release-fixture-", dir=Path(__file__).parent)
        self.root = Path(self.temp.name)
        self.stage = self.root / "stage"
        self.output = self.root / "dist"
        self.output.mkdir()
        pe(self.stage / release.DLL)
        pe(self.stage / release.REQUIRED[1])
        locale = self.stage / release.REQUIRED[2]
        locale.parent.mkdir(parents=True)
        locale.write_text('Name="ExcelProtocol FX"', encoding="utf-8")
        pe(self.stage / release.REQUIRED[3])
        pe(self.output / "ExcelProtocol-FX-1.0.0-windows-x64-setup.exe", 0x14C)
        self.commit = "a" * 40

    def tearDown(self):
        self.temp.cleanup()

    def build(self):
        return release.package(self.stage, self.output, "1.0.0", "example/ExcelProtocol-FX", self.commit)

    def test_versions(self):
        self.assertEqual(release.version_info("v1.0.0"), ("1.0.0", "stable"))
        self.assertEqual(release.version_info("1.2.3-beta.12"), ("1.2.3-beta.12", "beta"))
        for invalid in ("1.0", "01.0.0", "1.0.0-beta.0", "1.0.0-rc.1", "1.0.0\n", "1.0.0-beta.01"):
            with self.subTest(invalid=invalid), self.assertRaises(ValueError):
                release.version_info(invalid)

    def test_package_and_manifest(self):
        manifest = self.build()
        self.assertEqual(manifest["channel"], "stable")
        self.assertEqual(manifest["obs"]["minimum"], "32.2.2")
        release.validate_bundle(self.output, "1.0.0", "example/ExcelProtocol-FX", self.commit)

    def test_relative_stage_payload(self):
        previous = Path.cwd()
        try:
            os.chdir(self.root)
            release.make_payload(Path("stage"), Path("metadata"))
            self.assertTrue((self.root / "metadata/payload-files.txt").is_file())
        finally:
            os.chdir(previous)

    def test_tampered_installer_rejected(self):
        self.build()
        with (self.output / "ExcelProtocol-FX-1.0.0-windows-x64-setup.exe").open("ab") as stream:
            stream.write(b"tampered")
        with self.assertRaises(ValueError):
            release.validate_bundle(self.output, "1.0.0", "example/ExcelProtocol-FX", self.commit)

    def test_wrong_repository_rejected(self):
        self.build()
        with self.assertRaises(ValueError):
            release.validate_bundle(self.output, "1.0.0", "attacker/ExcelProtocol-FX", self.commit)

    def test_manifest_checksum_required(self):
        self.build()
        checksum = self.output / "SHA256SUMS.txt"
        checksum.write_text("\n".join(checksum.read_text().splitlines()[:2]) + "\n")
        with self.assertRaises(ValueError):
            release.validate_bundle(self.output, "1.0.0", "example/ExcelProtocol-FX", self.commit)

    def test_missing_helper_rejected(self):
        (self.stage / release.REQUIRED[1]).unlink()
        with self.assertRaises(ValueError):
            self.build()

    def test_missing_tls_backend_rejected(self):
        (self.stage / release.REQUIRED[3]).unlink()
        with self.assertRaises(ValueError):
            self.build()

    def test_wrong_architecture_rejected(self):
        pe(self.stage / release.DLL, 0x14C)
        with self.assertRaises(ValueError):
            self.build()

    def test_unrelated_payload_rejected(self):
        path = self.stage / "bin/64bit/obs64.exe"
        pe(path)
        with self.assertRaises(ValueError):
            self.build()

    def test_payload_path_escape_rejected(self):
        for name in ("../obs.dll", release.DATA + "../../evil", "C:/file", "/" + release.DLL, release.DATA + "x:stream", release.DATA + "..\\evil"):
            with self.subTest(name=name):
                self.assertFalse(release.safe_payload_name(name))

    def test_unapproved_qa_rejected(self):
        qa = self.root / "qa.json"
        qa.write_text(json.dumps({"version": "1.0.0", "approved": False}))
        with self.assertRaises(ValueError):
            release.validate_qa(qa, "1.0.0", self.root)

    def test_beta_does_not_require_stable_attestation(self):
        release.validate_qa(self.root / "absent.json", "1.0.0-beta.1", self.root)

    def test_qa_cannot_cover_later_source_change(self):
        def git(*args):
            return subprocess.check_output(["git", *args], cwd=self.root, text=True, stderr=subprocess.DEVNULL).strip()
        git("init")
        git("config", "user.email", "fixture@example.invalid")
        git("config", "user.name", "Release Test")
        (self.root / "source.cpp").write_text("first")
        git("add", "source.cpp")
        git("commit", "-m", "tested source")
        tested = git("rev-parse", "HEAD")
        (self.root / "source.cpp").write_text("different code")
        git("add", "source.cpp")
        git("commit", "-m", "untested change")
        qa = self.root / "qa.json"
        qa.write_text(json.dumps({"version": "1.0.0", "approved": True, "tester": "Fixture", "tested_commit": tested}))
        with self.assertRaisesRegex(ValueError, "invalidate stable QA"):
            release.validate_qa(qa, "1.0.0", self.root)


if __name__ == "__main__":
    unittest.main()
