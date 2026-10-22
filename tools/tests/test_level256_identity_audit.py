"""Tests for the read-only Level256 identity audit.

Run: python3 -m unittest discover -s tools/tests -p 'test_level256_identity_audit.py'
"""

from __future__ import annotations

import contextlib
import io
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from level256_identity_audit import FILES, assess_identity, inspect_instances


class Level256IdentityAuditTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.alpha = Path(self.temp.name) / "alpha"
        self.omega = Path(self.temp.name) / "omega"
        self.alpha.mkdir()
        self.omega.mkdir()

    def put(self, root: Path, name: str, data: bytes) -> None:
        path = root / FILES[name]
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)

    def test_cloned_seed_same_account_id_different_private_keys(self) -> None:
        for root in (self.alpha, self.omega):
            self.put(root, "LocalFriendCodeSeed_B", b"b" * 0x110)
            self.put(root, "account.id", b"account-one")
        self.put(self.alpha, "account.key", b"secret-alpha")
        self.put(self.omega, "account.key", b"secret-omega")

        result = inspect_instances(self.alpha, self.omega)
        self.assertEqual(result["LocalFriendCodeSeed_A"], "MISSING (both)")
        self.assertEqual(result["LocalFriendCodeSeed_B"], "IDENTICAL")
        self.assertEqual(result["account.id"], "IDENTICAL")
        self.assertEqual(result["account.key"], "DIFFERENT")
        warnings = " ".join(assess_identity(result))
        self.assertIn("ACCOUNT KEY CONFLICT", warnings)
        self.assertNotIn("secret-alpha", warnings)
        self.assertNotIn("secret-omega", warnings)

    def test_independent_seeds_and_accounts(self) -> None:
        for root, suffix in ((self.alpha, b"A"), (self.omega, b"B")):
            self.put(root, "LocalFriendCodeSeed_B", suffix * 0x110)
            self.put(root, "account.id", suffix * 16)
            self.put(root, "account.key", suffix * 32)

        results = inspect_instances(self.alpha, self.omega)
        self.assertTrue(all(value == "DIFFERENT" for name, value in results.items()
                            if name != "LocalFriendCodeSeed_A"))
        self.assertIn("No matching LFCS_B", assess_identity(results)[0])

    def test_same_account_id_and_key_warns_of_duplicate_credentials(self) -> None:
        for root in (self.alpha, self.omega):
            self.put(root, "account.id", b"id")
            self.put(root, "account.key", b"key")

        result = inspect_instances(self.alpha, self.omega)
        self.assertIn("DUPLICATE ACCOUNT CREDENTIALS", assess_identity(result)[0])

    def test_missing_account_key_is_reported_not_created(self) -> None:
        self.put(self.alpha, "account.id", b"same-id")
        self.put(self.omega, "account.id", b"same-id")
        self.put(self.alpha, "account.key", b"only-on-alpha")

        results = inspect_instances(self.alpha, self.omega)
        self.assertEqual(results["account.key"], "MISSING (omega)")
        self.assertFalse((self.omega / FILES["account.key"]).exists())

    def test_no_secret_content_is_printed_by_assessment(self) -> None:
        for root in (self.alpha, self.omega):
            self.put(root, "account.id", b"secret-account-id")
            self.put(root, "account.key", b"secret-key")
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            warnings = assess_identity(inspect_instances(self.alpha, self.omega))
            for warning in warnings:
                print(warning)
        self.assertNotIn("secret-account-id", out.getvalue())
        self.assertNotIn("secret-key", out.getvalue())


if __name__ == "__main__":
    unittest.main()
