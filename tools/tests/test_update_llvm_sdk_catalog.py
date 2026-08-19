import contextlib
import importlib.util
import io
import pathlib
import unittest


SCRIPT = pathlib.Path(__file__).parents[1] / "update-llvm-sdk-catalog.py"
SPEC = importlib.util.spec_from_file_location("update_llvm_sdk_catalog", SCRIPT)
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


def approved_asset(version: str) -> dict:
    return {
        "name": f"LLVM-{version}-Linux-X64.tar.xz",
        "digest": "sha256:" + "a" * 64,
        "size": 123,
        "browser_download_url": "https://example.test/llvm.tar.xz",
    }


def release_document(version: str) -> dict:
    return {
        "tag_name": f"llvmorg-{version}",
        "draft": False,
        "prerelease": False,
        "assets": [approved_asset(version)],
    }


class CatalogCandidate(unittest.TestCase):
    def assert_rejected(self, document: dict) -> None:
        with contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit):
            MODULE.catalog_candidate("22.1.8", document)

    def test_emits_only_the_approved_mapping(self) -> None:
        document = release_document("22.1.8")
        document["assets"].append({"name": "LLVM-22.1.8-Linux-ARM64.tar.xz"})
        candidate = MODULE.catalog_candidate("22.1.8", document)
        self.assertEqual(candidate["upstream-tag"], "llvmorg-22.1.8")
        self.assertEqual(len(candidate["artifacts"]), 1)
        self.assertEqual(candidate["artifacts"][0]["host"]["architecture"], "x86_64")

    def test_rejects_missing_digest(self) -> None:
        document = release_document("22.1.8")
        document["assets"][0]["digest"] = None
        self.assert_rejected(document)

    def test_rejects_prerelease(self) -> None:
        document = release_document("22.1.8")
        document["prerelease"] = True
        self.assert_rejected(document)

    def test_rejects_an_unapproved_only_release(self) -> None:
        document = release_document("22.1.8")
        document["assets"] = [{"name": "LLVM-22.1.8-Linux-ARM64.tar.xz"}]
        self.assert_rejected(document)

    def test_rejects_duplicate_approved_mapping(self) -> None:
        document = release_document("22.1.8")
        document["assets"].append(approved_asset("22.1.8"))
        self.assert_rejected(document)


if __name__ == "__main__":
    unittest.main()
