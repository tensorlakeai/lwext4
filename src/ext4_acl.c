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
 * @file  ext4_acl.c
 * @brief POSIX ACL on-disk / userspace xattr format conversion. See
 *        ext4_acl.h for the format definitions and rationale.
 */

#include <ext4_config.h>
#include <ext4_acl.h>
#include <ext4_errno.h>
#include <ext4_misc.h>

#include <stdbool.h>
#include <string.h>

/* Userspace xattr entry: { __le16 e_tag; __le16 e_perm; __le32 e_id; } — 8B */
#define ACL_XATTR_HEADER_SIZE    4u
#define ACL_XATTR_ENTRY_SIZE     8u

/* On-disk entry sizes: short (no e_id) for OBJ/MASK/OTHER, full for USER/GROUP. */
#define ACL_DISK_HEADER_SIZE     4u
#define ACL_DISK_ENTRY_SHORT     4u
#define ACL_DISK_ENTRY_FULL      8u

static uint16_t read_le16(const uint8_t *p)
{
	return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t read_le32(const uint8_t *p)
{
	return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
	       ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void write_le16(uint8_t *p, uint16_t v)
{
	p[0] = (uint8_t)(v & 0xff);
	p[1] = (uint8_t)((v >> 8) & 0xff);
}

static void write_le32(uint8_t *p, uint32_t v)
{
	p[0] = (uint8_t)(v & 0xff);
	p[1] = (uint8_t)((v >> 8) & 0xff);
	p[2] = (uint8_t)((v >> 16) & 0xff);
	p[3] = (uint8_t)((v >> 24) & 0xff);
}

/* True if the tag is stored as a short (4-byte) entry on disk. */
static bool tag_is_short_on_disk(uint16_t tag)
{
	return tag == ACL_USER_OBJ || tag == ACL_GROUP_OBJ ||
	       tag == ACL_MASK || tag == ACL_OTHER;
}

/* True if the tag carries an e_id in the userspace xattr form. */
static bool tag_is_named(uint16_t tag)
{
	return tag == ACL_USER || tag == ACL_GROUP;
}

int ext4_acl_to_disk(const void *xattr_value, size_t xattr_size,
		     void *out_buf, size_t out_buf_size, size_t *needed)
{
	const uint8_t *in;
	uint8_t *out;
	uint32_t version;
	size_t off;
	size_t out_size;

	if (!xattr_value || !needed)
		return EINVAL;
	if (xattr_size < ACL_XATTR_HEADER_SIZE)
		return EINVAL;

	in = (const uint8_t *)xattr_value;
	version = read_le32(in);
	if (version != POSIX_ACL_XATTR_VERSION)
		return EINVAL;

	if ((xattr_size - ACL_XATTR_HEADER_SIZE) % ACL_XATTR_ENTRY_SIZE != 0)
		return EINVAL;

	/* First pass: compute output size. */
	out_size = ACL_DISK_HEADER_SIZE;
	for (off = ACL_XATTR_HEADER_SIZE; off < xattr_size;
	     off += ACL_XATTR_ENTRY_SIZE) {
		uint16_t tag = read_le16(in + off);
		out_size += tag_is_short_on_disk(tag) ? ACL_DISK_ENTRY_SHORT
						      : ACL_DISK_ENTRY_FULL;
	}
	*needed = out_size;
	if (!out_buf)
		return EOK;
	if (out_buf_size < out_size)
		return ERANGE;

	/* Second pass: emit. */
	out = (uint8_t *)out_buf;
	write_le32(out, EXT4_ACL_VERSION);
	out += ACL_DISK_HEADER_SIZE;
	for (off = ACL_XATTR_HEADER_SIZE; off < xattr_size;
	     off += ACL_XATTR_ENTRY_SIZE) {
		uint16_t tag = read_le16(in + off);
		uint16_t perm = read_le16(in + off + 2);
		uint32_t id = read_le32(in + off + 4);

		write_le16(out, tag);
		write_le16(out + 2, perm);
		out += 4;
		if (!tag_is_short_on_disk(tag)) {
			write_le32(out, id);
			out += 4;
		}
	}
	return EOK;
}

int ext4_acl_from_disk(const void *ondisk_value, size_t ondisk_size,
		       void *out_buf, size_t out_buf_size, size_t *needed)
{
	const uint8_t *in;
	const uint8_t *end;
	uint8_t *out;
	uint32_t version;
	size_t entries;
	size_t out_size;
	const uint8_t *cursor;

	if (!ondisk_value || !needed)
		return EINVAL;
	if (ondisk_size < ACL_DISK_HEADER_SIZE)
		return EINVAL;

	in = (const uint8_t *)ondisk_value;
	version = read_le32(in);
	if (version != EXT4_ACL_VERSION)
		return EINVAL;

	/* Walk variable-length entries once to count them and validate
	 * that each declares a known tag with the matching on-disk size. */
	end = in + ondisk_size;
	cursor = in + ACL_DISK_HEADER_SIZE;
	entries = 0;
	while (cursor < end) {
		uint16_t tag;
		size_t step;

		if ((size_t)(end - cursor) < ACL_DISK_ENTRY_SHORT)
			return EINVAL;
		tag = read_le16(cursor);

		if (tag_is_short_on_disk(tag)) {
			step = ACL_DISK_ENTRY_SHORT;
		} else if (tag_is_named(tag)) {
			if ((size_t)(end - cursor) < ACL_DISK_ENTRY_FULL)
				return EINVAL;
			step = ACL_DISK_ENTRY_FULL;
		} else {
			return EINVAL;
		}
		cursor += step;
		entries++;
	}
	if (cursor != end)
		return EINVAL;

	out_size = ACL_XATTR_HEADER_SIZE + entries * ACL_XATTR_ENTRY_SIZE;
	*needed = out_size;
	if (!out_buf)
		return EOK;
	if (out_buf_size < out_size)
		return ERANGE;

	out = (uint8_t *)out_buf;
	write_le32(out, POSIX_ACL_XATTR_VERSION);
	out += ACL_XATTR_HEADER_SIZE;

	cursor = in + ACL_DISK_HEADER_SIZE;
	while (cursor < end) {
		uint16_t tag = read_le16(cursor);
		uint16_t perm = read_le16(cursor + 2);
		uint32_t id;

		if (tag_is_short_on_disk(tag)) {
			id = ACL_UNDEFINED_ID;
			cursor += ACL_DISK_ENTRY_SHORT;
		} else {
			id = read_le32(cursor + 4);
			cursor += ACL_DISK_ENTRY_FULL;
		}
		write_le16(out, tag);
		write_le16(out + 2, perm);
		write_le32(out + 4, id);
		out += ACL_XATTR_ENTRY_SIZE;
	}
	return EOK;
}

/**
 * @}
 */
