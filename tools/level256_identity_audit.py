#!/usr/bin/env python3
"""Read-only audit of Level256 identity files in two separate Azahar instances.

This tool compares file contents in memory. It does not print secret bytes,
fingerprints, IDs, or keys and never modifies instance data.
"""

from __future__ import annotations

import argparse
from pathlib import Path

FILES = {
    "LocalFriendCodeSeed_A": Path("user/nand/rw/sys/LocalFriendCodeSeed_A"),
    "LocalFriendCodeSeed_B": Path("user/nand/rw/sys/LocalFriendCodeSeed_B"),
    "account.id": Path("user/sdmc/Level256/account.id"),
    "account.key": Path("user/sdmc/Level256/account.key"),
}


def compare_files(left: Path, right: Path) -> str:
    left_exists = left.is_file()
    right_exists = right.is_file()
    if not left_exists or not right_exists:
        if left_exists:
            return "MISSING (omega)"
        if right_exists:
            return "MISSING (alpha)"
        return "MISSING (both)"

    # A full comparison, not just metadata or matching size/mtime.
    with left.open("rb") as first, right.open("rb") as second:
        while True:
            left_chunk = first.read(64 * 1024)
            right_chunk = second.read(64 * 1024)
            if left_chunk != right_chunk:
                return "DIFFERENT"
            if not left_chunk:
                return "IDENTICAL"


def inspect_instances(alpha: Path, omega: Path) -> dict[str, str]:
    return {
        name: compare_files(alpha / relative_path, omega / relative_path)
        for name, relative_path in FILES.items()
    }


def assess_identity(results: dict[str, str]) -> list[str]:
    """Return findings without claiming knowledge of Level256's server policy."""
    findings: list[str] = []
    same_seed = results["LocalFriendCodeSeed_B"] == "IDENTICAL"
    same_id = results["account.id"] == "IDENTICAL"
    same_key = results["account.key"] == "IDENTICAL"
    different_key = results["account.key"] == "DIFFERENT"

    if same_seed:
        findings.append(
            "The two instances contain the same signed LFCS_B device data. "
            "Changing only Console ID or MAC does not change this file."
        )
    if same_id and different_key:
        findings.append(
            "ACCOUNT KEY CONFLICT: account.id is identical while account.key "
            "differs. If the server binds an account ID to a public signing "
            "key, one instance may fail with SignatureInvalid."
        )
    elif same_id and same_key:
        findings.append(
            "DUPLICATE ACCOUNT CREDENTIALS: the same account ID and key exist "
            "in both instances. Concurrent sessions might conflict."
        )
    elif same_id:
        findings.append(
            "The account ID is shared, but at least one account key could "
            "not be compared."
        )

    if not findings:
        findings.append(
            "No matching LFCS_B or conflicting account ID/key pair was found "
            "among the files that could be compared. Server behavior is unverified."
        )
    return findings


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("alpha", type=Path, help="First Azahar instance root")
    parser.add_argument("omega", type=Path, help="Second Azahar instance root")
    args = parser.parse_args()
    alpha = args.alpha.expanduser().resolve()
    omega = args.omega.expanduser().resolve()
    if alpha == omega or not alpha.is_dir() or not omega.is_dir():
        parser.error("Provide two different, existing instance root directories")

    try:
        results = inspect_instances(alpha, omega)
    except OSError as error:
        parser.error(f"Cannot read instance files: {error.strerror}")

    for name, status in results.items():
        print(f"{name}: {status}")

    print()
    for finding in assess_identity(results):
        print(f"WARNING: {finding}")

    print(
        "\nRead-only diagnostic. No account/key material is displayed or modified. "
        "SignatureInvalid cannot be attributed to the server or emulator "
        "without an authenticated protocol trace."
    )
    return int(
        results["account.id"] == "IDENTICAL"
        and results["account.key"] in ("IDENTICAL", "DIFFERENT")
        or results["LocalFriendCodeSeed_B"] == "IDENTICAL"
    )


if __name__ == "__main__":
    raise SystemExit(main())
