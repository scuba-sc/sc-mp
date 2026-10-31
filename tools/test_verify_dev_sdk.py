import hashlib
import json
from pathlib import Path
import tempfile
import unittest
import zipfile

from verify_dev_sdk import verify_pair


class VerifyDevelopmentSdk(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.commit = "a" * 40
        self.sdk = self.write_archive("sco-sdk-2.4.0", "legacy", self.commit)
        self.source = self.write_archive("sco-sdk-core-source-2.4.0", "core-source", self.commit)

    def write_archive(self, stem, profile, commit, abi="1.1"):
        path = self.root / (stem + ".zip")
        data = {"schema_version": 1, "profile": profile, "core_sdk_version": "2.4.0",
                "plugin_abi_version": abi, "source_commits": {"core": commit}}
        if profile == "legacy":
            data["source_commits"]["game-sc"] = commit
        with zipfile.ZipFile(path, "w") as archive:
            archive.writestr(stem + "/COMPATIBILITY.json", json.dumps(data))
        return hashlib.sha256(path.read_bytes()).hexdigest()

    def verify(self):
        verify_pair(self.root, "2.4.0", self.commit, self.sdk, self.source)

    def test_matching_pair(self):
        self.verify()

    def test_changed_archive_rejected(self):
        with (self.root / "sco-sdk-2.4.0.zip").open("ab") as file:
            file.write(b"changed")
        with self.assertRaisesRegex(ValueError, "SHA-256 mismatch"):
            self.verify()

    def test_mixed_source_rejected_even_if_hash_matches(self):
        self.source = self.write_archive("sco-sdk-core-source-2.4.0", "core-source", "b" * 40)
        with self.assertRaisesRegex(ValueError, "compatibility metadata"):
            self.verify()

    def test_mismatched_profile_and_abi_rejected(self):
        self.source = self.write_archive("sco-sdk-core-source-2.4.0", "legacy", self.commit)
        with self.assertRaisesRegex(ValueError, "compatibility metadata"):
            self.verify()
        self.source = self.write_archive("sco-sdk-core-source-2.4.0", "core-source", self.commit, "9.9")
        with self.assertRaisesRegex(ValueError, "ABI versions differ"):
            self.verify()

    def test_full_source_and_hashes_required(self):
        with self.assertRaisesRegex(ValueError, "full lowercase"):
            verify_pair(self.root, "2.4.0", "a" * 7, self.sdk, self.source)
        with self.assertRaisesRegex(ValueError, "lowercase SHA-256"):
            verify_pair(self.root, "2.4.0", self.commit, "0", self.source)


if __name__ == "__main__":
    unittest.main()
