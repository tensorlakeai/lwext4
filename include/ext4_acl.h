/*
 * Copyright (c) 2026 Tensorlake, Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * - Redistributions of source code must retain the above copyright
 *   notice, this list of conditions and the following disclaimer.
 * - Redistributions in binary form must reproduce the above copyright
 *   notice, this list of conditions and the following disclaimer in the
 *   documentation and/or other materials provided with the distribution.
 * - The name of the author may not be used to endorse or promote products
 *   derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/** @addtogroup lwext4
 * @{
 */
/**
 * @file  ext4_acl.h
 * @brief POSIX ACL on-disk / userspace xattr format conversion.
 *
 * Linux uses two distinct binary formats for POSIX ACLs:
 *
 *   - Userspace xattr format (what `setxattr` / `getxattr` syscalls and
 *     PAX `SCHILY.xattr.system.posix_acl_*` records exchange): header
 *     `a_version = POSIX_ACL_XATTR_VERSION (2)`, every entry is 8 bytes:
 *         { __le16 e_tag; __le16 e_perm; __le32 e_id; }
 *
 *   - ext4 on-disk format (what ext4 actually stores in the inode's
 *     xattr area, see `fs/ext4/acl.h` in Linux): header
 *     `a_version = EXT4_ACL_VERSION (1)`, entries are 4-byte "short" for
 *     ACL_USER_OBJ / ACL_GROUP_OBJ / ACL_MASK / ACL_OTHER (no e_id), 8-byte
 *     full for ACL_USER / ACL_GROUP (with e_id).
 *
 * The Linux kernel's `fs/ext4/acl.c` translates between these in its xattr
 * handler. lwext4 is a userspace ext4 driver; this module mirrors that
 * behaviour so writes via `ext4_setxattr("system.posix_acl_access", ...)`
 * land on disk in the format the kernel's `ext4_acl_from_disk` expects,
 * and reads via `ext4_getxattr(...)` return the userspace format.
 *
 * Without this conversion, lwext4 stores the userspace bytes verbatim and
 * the kernel returns EINVAL on `getfacl`.
 */

#ifndef EXT4_ACL_H_
#define EXT4_ACL_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdint.h>

#define EXT4_ACL_VERSION         0x0001
#define POSIX_ACL_XATTR_VERSION  0x0002

/* Xattr name-index values for the two POSIX ACL xattrs. These mirror the
 * private constants in ext4_xattr.c so call sites that dispatch on the
 * name_index (e.g. ext4.c's ext4_setxattr / ext4_getxattr) can branch
 * without depending on internal symbols. */
#define EXT4_XATTR_INDEX_POSIX_ACL_ACCESS_VAL   2
#define EXT4_XATTR_INDEX_POSIX_ACL_DEFAULT_VAL  3

/* Userspace POSIX ACL tag values (uapi/linux/posix_acl.h). */
#define ACL_USER_OBJ  0x01
#define ACL_USER      0x02
#define ACL_GROUP_OBJ 0x04
#define ACL_GROUP     0x08
#define ACL_MASK      0x10
#define ACL_OTHER     0x20

#define ACL_UNDEFINED_ID ((uint32_t)-1)

/**
 * @brief Convert userspace POSIX ACL xattr bytes into the ext4 on-disk
 *        representation.
 *
 * Two-phase API:
 *   - Call with `out_buf == NULL`, `out_buf_size == 0` to discover the
 *     required output size; the function writes the size into `*needed`
 *     and returns EOK without copying.
 *   - Call with a sized buffer. Returns EOK on success, ERANGE if
 *     `out_buf_size < needed` (`*needed` set), EINVAL on malformed input.
 *
 * @param xattr_value    Userspace xattr bytes (POSIX_ACL_XATTR_VERSION).
 * @param xattr_size     Length of @p xattr_value in bytes.
 * @param out_buf        Destination buffer for the on-disk representation,
 *                       or NULL for size probe.
 * @param out_buf_size   Capacity of @p out_buf.
 * @param needed         Output: number of bytes the conversion needs.
 * @return EOK on success; EINVAL / ERANGE / EIO on failure.
 */
int ext4_acl_to_disk(const void *xattr_value, size_t xattr_size,
		     void *out_buf, size_t out_buf_size, size_t *needed);

/**
 * @brief Convert ext4 on-disk POSIX ACL bytes into the userspace xattr
 *        representation. Same two-phase API as `ext4_acl_to_disk`.
 *
 * @param ondisk_value   On-disk bytes (EXT4_ACL_VERSION).
 * @param ondisk_size    Length of @p ondisk_value in bytes.
 * @param out_buf        Destination buffer for the userspace representation,
 *                       or NULL for size probe.
 * @param out_buf_size   Capacity of @p out_buf.
 * @param needed         Output: number of bytes the conversion needs.
 * @return EOK on success; EINVAL / ERANGE / EIO on failure.
 */
int ext4_acl_from_disk(const void *ondisk_value, size_t ondisk_size,
		       void *out_buf, size_t out_buf_size, size_t *needed);

#ifdef __cplusplus
}
#endif

#endif /* EXT4_ACL_H_ */

/**
 * @}
 */
