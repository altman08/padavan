#!/usr/bin/env python3
"""Check overlay isolation and rejection of incompatible Go sources."""

import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest


class OverlayTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.base = Path(self.tmp.name)
        root = Path(subprocess.check_output(
            [os.environ.get("GO", "go"), "env", "GOROOT"], universal_newlines=True).strip())
        self.source = self.base / "goroot/src/crypto/internal/fips140/aes"
        for name in ("aes_noasm.go", "cbc.go", "gcm/gcm.go", "gcm/gcm_noasm.go"):
            dst = self.source / name
            dst.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(root / "src/crypto/internal/fips140/aes" / name, dst)
        self.env = {"GOROOT": str(self.base / "goroot"), "GOVERSION": "go1.27.2"}
        self.go = self.base / "go"
        self.out = self.base / "overlay"

    def generate(self):
        self.go.write_text("#!" + sys.executable + "\nprint(" + repr(json.dumps(self.env)) + ")\n")
        self.go.chmod(0o755)
        return subprocess.run([
            sys.executable, str(Path(__file__).with_name("overlay.py")),
            "--go", str(self.go), "--out", str(self.out),
        ], stdout=subprocess.PIPE, stderr=subprocess.PIPE, universal_newlines=True)

    def test_private_overlay(self):
        original = {p: p.read_bytes() for p in self.source.rglob("*.go")}
        result = self.generate()
        self.assertEqual(result.returncode, 0, result.stderr)
        for path, data in original.items():
            self.assertEqual(path.read_bytes(), data, str(path))
        mapping = json.loads((self.out / "overlay.json").read_text())["Replace"]
        self.assertTrue(mapping)
        for source, replacement in mapping.items():
            self.assertTrue(Path(source).relative_to(self.source).parts)
            self.assertTrue(Path(replacement).relative_to(self.out).parts)
            self.assertTrue(Path(replacement).is_file())

    def test_unsupported_version(self):
        self.env["GOVERSION"] = "go1.28.0"
        self.assertNotEqual(self.generate().returncode, 0)
        self.assertFalse((self.out / "overlay.json").exists())

    def test_changed_source(self):
        path = self.source / "gcm/gcm.go"
        path.write_text(path.read_text().replace("\tgcmPlatformData\n", "\tchangedPlatformData\n"))
        self.assertNotEqual(self.generate().returncode, 0)
        self.assertFalse((self.out / "overlay.json").exists())

    def test_ambiguous_source(self):
        path = self.source / "gcm/gcm.go"
        path.write_text(path.read_text() + "\tgcmPlatformData\n")
        self.assertNotEqual(self.generate().returncode, 0)
        self.assertFalse((self.out / "overlay.json").exists())

    def test_changed_cbc_source(self):
        path = self.source / "cbc.go"
        path.write_text(path.read_text().replace("\tcryptBlocksDec(&c.b, &c.iv, dst, src)", "\tchangedCBC()"))
        self.assertNotEqual(self.generate().returncode, 0)
        self.assertFalse((self.out / "overlay.json").exists())


if __name__ == "__main__":
    unittest.main()
