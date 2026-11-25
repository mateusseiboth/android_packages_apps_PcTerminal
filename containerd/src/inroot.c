#define _GNU_SOURCE
#include "inroot.h"

#include <errno.h>
#include <fcntl.h>
#include <linux/openat2.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>

#ifndef SYS_openat2
#define SYS_openat2 437
#endif

static int is_skipped(const char *part, size_t len)
{
	return len == 0 || (len == 1 && part[0] == '.');
}

int normalize_rel(const char *in, char *out, size_t cap)
{
	size_t n = 0;
	for (const char *p = in; *p;) {
		size_t len = strcspn(p, "/");
		if (len == 2 && !strncmp(p, "..", 2))
			return -1;
		if (!is_skipped(p, len)) {
			if (n + (n > 0) + len + 1 > cap)
				return -1;
			if (n)
				out[n++] = '/';
			memcpy(out + n, p, len);
			n += len;
		}
		p += len + (p[len] == '/');
	}
	if (!cap)
		return -1;
	out[n] = 0;
	return 0;
}

int open_in_root(int root_fd, const char *rel, int flags, mode_t mode)
{
	struct open_how how = {
		.flags = (unsigned long long)(flags | O_CLOEXEC),
		.mode = (flags & (O_CREAT | O_TMPFILE)) ? mode : 0,
		.resolve = RESOLVE_IN_ROOT | RESOLVE_NO_MAGICLINKS,
	};
	return (int)syscall(SYS_openat2, root_fd, *rel ? rel : ".", &how, sizeof how);
}

/* One more folder below dirfd: made when missing (reported to on_created), then opened. */
static int step_into(int root_fd, const char *prefix, const char *name, mode_t mode, created_fn on_created, void *ctx)
{
	int parent = open_in_root(root_fd, prefix, O_PATH | O_DIRECTORY, 0);
	if (parent < 0)
		return -1;
	int made = !mkdirat(parent, name, mode);
	int failed = !made && errno != EEXIST;
	failed = failed || (made && on_created && on_created(ctx, parent, name));
	close(parent);
	return failed ? -1 : 0;
}

int make_dirs_in_root(int root_fd, const char *rel, mode_t mode, created_fn on_created, void *ctx)
{
	char path[4096];
	if (normalize_rel(rel, path, sizeof path))
		return -1;
	for (char *slash = path; slash && *path;) {
		slash = strchr(slash, '/');
		if (slash)
			*slash = 0;
		char *name = strrchr(path, '/');
		char prefix[4096];
		snprintf(prefix, sizeof prefix, "%.*s", name ? (int)(name - path) : 0, path);
		if (step_into(root_fd, prefix, name ? name + 1 : path, mode, on_created, ctx))
			return -1;
		if (slash)
			*slash++ = '/';
	}
	int fd = open_in_root(root_fd, path, O_PATH | O_DIRECTORY, 0);
	if (fd < 0)
		return -1;
	close(fd);
	return 0;
}

int open_parent_in_root(int root_fd, const char *rel, char *base, size_t cap, created_fn on_created, void *ctx)
{
	char path[4096];
	if (normalize_rel(rel, path, sizeof path) || !*path)
		return -1;
	char *slash = strrchr(path, '/');
	const char *name = slash ? slash + 1 : path;
	if (strlen(name) + 1 > cap)
		return -1;
	snprintf(base, cap, "%s", name);
	if (slash)
		*slash = 0;
	else
		path[0] = 0;
	if (*path && make_dirs_in_root(root_fd, path, 0755, on_created, ctx))
		return -1;
	return open_in_root(root_fd, path, O_PATH | O_DIRECTORY, 0);
}
