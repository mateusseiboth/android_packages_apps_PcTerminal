/*
 * Files inside a container's rootfs, opened from the host by root. Every lookup goes through
 * openat2(RESOLVE_IN_ROOT): ".." and absolute symbolic links stop at the rootfs, as they would inside
 * the container, so a rootfs can never point the daemon at a host file.
 */
#pragma once
#include <stddef.h>
#include <sys/types.h>

/* "./a//b/./c/" -> "a/b/c"; "" is the root itself. -1 on ".." components or when it does not fit. */
int normalize_rel(const char *in, char *out, size_t cap);
/* openat2 below root_fd with RESOLVE_IN_ROOT | RESOLVE_NO_MAGICLINKS; -1 and errno on failure. */
int open_in_root(int root_fd, const char *rel, int flags, mode_t mode);

typedef int (*created_fn)(void *ctx, int dirfd, const char *name);
/* mkdir -p of rel inside the root; on_created (may be NULL) sees each folder it made. */
int make_dirs_in_root(int root_fd, const char *rel, mode_t mode, created_fn on_created, void *ctx);
/* The folder holding rel (made when missing) as an O_PATH fd, and rel's last component in base. */
int open_parent_in_root(int root_fd, const char *rel, char *base, size_t cap, created_fn on_created, void *ctx);
