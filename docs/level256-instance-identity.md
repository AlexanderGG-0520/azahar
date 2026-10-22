# Level256 account identity on cloned Azahar instances

## Scope and evidence

This is a **read-only diagnostic**, not a repair or an account provisioning implementation.

Two separate Azahar instances were copied from one user directory, then had their
Console ID and MAC address regenerated. The reported comparison after reconnecting
to Level256 was:

| Local artifact | alpha vs omega |
| --- | --- |
| `user/nand/rw/sys/LocalFriendCodeSeed_A` | absent in one or both |
| `user/nand/rw/sys/LocalFriendCodeSeed_B` | **identical** |
| `user/sdmc/Level256/account.id` | **identical** |
| `user/sdmc/Level256/account.key` | **different** |

One instance displayed `0x0026 SignatureInvalid` while testing concurrent
connections. Previously deleting local account files enabled a fresh registration;
regenerating only Console ID and MAC did not resolve the issue.

**The account ID/key disagreement is real, but it is not proof of the remote
server's account model.** If that server associates one account ID with a
particular public signing key, the same ID signed with another key will fail
signature verification. Whether the server identifies an account using LFCS_B,
account.id, a combination, or other fields requires a protocol-level check or
server-side documentation. A shared public IP alone does not explain this
local inconsistency.

The Level256 Luma3DS source
([`l256_lfcs.c`](https://github.com/Tekito-256/Luma3DS-level256/blob/master/sysmodules/rosalina/source/plugin/l256_lfcs.c))
selects `LocalFriendCodeSeed_B` before `LocalFriendCodeSeed_A` and exposes it
to an authorized Level256 plugin via `plg:ldr` command `0x2560`. Azahar's
Level256 compatibility implementation follows that priority and reads the
LFCS from **the active instance's NAND**. It does not generate account IDs,
issue account keys, or verify Level256 server-side signatures.

The Level256 installer
([README](https://github.com/Tekito-256/level256-installer/blob/main/README.md))
states that accounts are device-bound and that normal installation preserves
`/Level256/account.key`. This supports treating copied device and account
state as a potential registration conflict, rather than interpreting
`SignatureInvalid` as proof of an IP-based connection limit.

## Reproduce the local diagnosis without changing files

Run with both instance paths. The script never prints account contents,
private keys, LFCS data, or hashes and never modifies any files:

```sh
python3 tools/level256_identity_audit.py \
  "$HOME/azahar/azahar-alpha" \
  "$HOME/azahar/azahar-omega"
```

The script reports `IDENTICAL`, `DIFFERENT`, or which instance is
`MISSING` for each of four files. Exit status `1` means cloned
device data or duplicated/conflicting account state was found; `0` means no
such condition was identified in the files present, **not** that remote login
will succeed. Exit status `2` means invocation/read error.

The `LocalFriendCodeSeed_A` file is often absent and its absence alone does
not prove corruption. The LFCS_B data is signed hardware-identity data.
Do not randomize it, clone account keys between different accounts, or bypass
signature checks just to remove the error.

## Separate connection/session conflicts from persistent credentials

Use the same saved game and plugin configuration, without copying any more
account files:

1. Fully close both instances. Start **alpha only** and record whether
   registration/login succeeds.
2. Fully exit alpha. Start **omega only** and record the result.
3. Connect alpha, then connect omega while alpha remains online.
4. Disconnect both. Reverse the order: omega, then alpha.

If one instance fails **alone**, investigate its persisted account
registration/key consistency first. If both work **alone** but the second
always fails regardless of order, a duplicate device/session restriction or
server-side replacement of the registered key is plausible. These are
hypotheses, not proven backend behavior.

When recording a failure, capture non-secret status/result codes and the
relative sequence of login attempts. Never upload `account.key`, LFCS_B,
`otp.bin`, `movable.sed`, or token-bearing HTTP payloads to a public issue.

## Implementation boundary

- Do **not** automatically regenerate account files from within Azahar: the
  account format and Level256 enrollment protocol are not implemented here.
- Do **not** alter or fabricate signed LFCS_B data; it must remain authentic.
- Do **not** globally change UDS/Pretendo, the Console ID, or the MAC address
  to work around an unverified server condition.
- A complete fix for intentional multi-instance use depends on independent,
  legitimately provisioned device identities and account enrollment supported
  by the remote service, or a documented multi-session policy.

Regression tests for the read-only audit can be run with:

```sh
python3 -m unittest discover -s tools/tests -p 'test_level256_identity_audit.py'
```
