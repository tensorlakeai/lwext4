# Tensorlake fork of lwext4

This repository is `tensorlakeai/lwext4`, a fork of
[gkostka/lwext4][upstream].

## Branch conventions

| Branch | What it is |
|---|---|
| `master` | Mirror of upstream `gkostka/lwext4` `master`. Tracks the public project. Do **not** commit Tensorlake-specific changes here. |
| `tensorlake-master` | **The canonical Tensorlake fork tip.** Contains upstream `master` plus Tensorlake-authored patches that upstream has not (yet) accepted. Downstream Tensorlake products (`tensorlakeai/ext4-lwext4`, `tensorlakeai/compute-engine-internal`) consume this branch by name. |
| Other branches (e.g. `eugene/*`, `fix/*`) | Historical feature branches; do not depend on them. They may be deleted without notice. |

Current Tensorlake-authored patches on `tensorlake-master` (relative to upstream
`master`):

- `ext4_dir_idx`: zero `name_len` / `file_type` in the non-`metadata_csum`
  `dx_init` leaf so freshly-allocated bcache bytes don't leak onto disk and
  trip the Linux kernel ext4 validator on first write to the affected
  directory.
- `mkfs`: extend `EXT4_SUPPORTED_FCOM` with `EXT4_FCOM_EXT_ATTR` so
  freshly-formatted images advertise xattr support in the superblock and
  Linux retains xattrs written via `ext4_xattr_set`.
- `ext4_acl`: translate POSIX ACL bytes between the userspace xattr format
  (`POSIX_ACL_XATTR_VERSION=2`, always-8-byte entries) and the ext4
  on-disk format (`EXT4_ACL_VERSION=1`, short 4-byte entries for
  `USER_OBJ`/`GROUP_OBJ`/`MASK`/`OTHER`) inside `ext4_setxattr` /
  `ext4_getxattr`. Mirrors the Linux kernel's `fs/ext4/acl.c` semantics so
  ACLs written through lwext4 are readable by the kernel and vice versa.

## Rebasing / merging

`tensorlake-master` is **append-only** from downstream's perspective: future
Tensorlake patches land as new commits on top, never via force-push or history
rewrite (would invalidate every downstream `Cargo.lock` that pins this
branch). Upstream resyncs happen by merging upstream `master` into
`tensorlake-master`, not the other way around.

If a Tensorlake-authored patch is later accepted upstream, the corresponding
commit on `tensorlake-master` becomes redundant; remove it during the next
upstream resync merge.

[upstream]: https://github.com/gkostka/lwext4
