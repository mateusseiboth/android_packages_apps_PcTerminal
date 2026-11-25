/*
 * Extracts an uncompressed tar stream (ustar, GNU long names, pax records) into a container's rootfs.
 * Owners are shifted into the container's host id range; device nodes are skipped (the container gets
 * its /dev from the host); every path is resolved inside the root (inroot.h).
 */
#pragma once

struct tar_progress {
	unsigned long long bytes; /* tar bytes read */
	unsigned long long files; /* entries extracted */
};

struct tar_target {
	int root_fd;      /* the rootfs folder */
	unsigned id_base; /* host id of the container's id 0 */
	/* lchown of name in dirfd ("" is dirfd itself); the device calls fchownat. */
	int (*chown_at)(void *ctx, int dirfd, const char *name, unsigned uid, unsigned gid);
	void *ctx;
	/* Called after every entry (may be NULL). */
	void (*on_progress)(void *ctx, const struct tar_progress *p);
};

/* NULL when done, else a PROTOCOL.md error code: bad_archive, no_space or io. progress may be NULL. */
const char *extract_tar(int fd, const struct tar_target *t, struct tar_progress *progress);
