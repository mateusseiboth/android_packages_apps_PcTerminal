#define _GNU_SOURCE
#include "util.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

int read_text_file(const char *path, char *out, size_t cap)
{
	int fd = open(path, O_RDONLY | O_CLOEXEC);
	if (fd < 0)
		return -1;
	size_t got = 0;
	ssize_t n = 1;
	while (got < cap && (n = read(fd, out + got, cap - got)) > 0)
		got += (size_t)n;
	close(fd);
	if (n < 0 || got >= cap)
		return -1;
	out[got] = 0;
	return 0;
}

int save_text_file(const char *path, const char *text, unsigned mode)
{
	char tmp[PATH_MAX];
	snprintf(tmp, sizeof tmp, "%s.tmp", path);
	int fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC | O_NOFOLLOW, mode);
	if (fd < 0)
		return -1;
	size_t len = strlen(text);
	int failed = write(fd, text, len) != (ssize_t)len || fsync(fd);
	failed = close(fd) || failed;
	return failed || rename(tmp, path) ? -1 : 0;
}

int make_dirs(const char *path, unsigned mode)
{
	char buf[PATH_MAX];
	snprintf(buf, sizeof buf, "%s", path);
	for (char *p = buf + 1; *p; p++) {
		if (*p != '/')
			continue;
		*p = 0;
		if (mkdir(buf, mode) && errno != EEXIST)
			return -1;
		*p = '/';
	}
	return mkdir(buf, mode) && errno != EEXIST ? -1 : 0;
}

static int open_subdir(int dirfd, const char *name)
{
	int flags = O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC;
	int fd = openat(dirfd, name, flags);
	if (fd >= 0 || errno != EACCES)
		return fd;
	/* A folder the rootfs made unreadable (chmod 000): root may, a host test needs to ask. */
	fchmodat(dirfd, name, 0700, 0);
	return openat(dirfd, name, flags);
}

static int remove_children(int fd, dev_t dev)
{
	DIR *d = fdopendir(fd);
	if (!d) {
		close(fd);
		return -1;
	}
	int failed = 0;
	for (struct dirent *e; (e = readdir(d));) {
		if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, ".."))
			continue;
		struct stat st;
		if (fstatat(dirfd(d), e->d_name, &st, AT_SYMLINK_NOFOLLOW))
			continue;
		if (!S_ISDIR(st.st_mode)) {
			failed |= unlinkat(dirfd(d), e->d_name, 0) && errno != ENOENT;
			continue;
		}
		if (st.st_dev != dev) {
			failed = 1; /* something is mounted here: leave it */
			continue;
		}
		int sub = open_subdir(dirfd(d), e->d_name);
		failed |= sub < 0 || remove_children(sub, dev) || unlinkat(dirfd(d), e->d_name, AT_REMOVEDIR);
	}
	closedir(d);
	return failed ? -1 : 0;
}

int remove_tree_at(int dirfd, const char *name)
{
	struct stat st;
	if (fstatat(dirfd, name, &st, AT_SYMLINK_NOFOLLOW))
		return errno == ENOENT ? 0 : -1;
	if (!S_ISDIR(st.st_mode))
		return unlinkat(dirfd, name, 0);
	int fd = open_subdir(dirfd, name);
	if (fd < 0 || remove_children(fd, st.st_dev))
		return -1;
	return unlinkat(dirfd, name, AT_REMOVEDIR);
}
