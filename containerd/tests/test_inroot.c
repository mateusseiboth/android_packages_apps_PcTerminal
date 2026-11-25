/* Paths inside a container's root: never leave it, whatever the links in the rootfs say. */
#define _GNU_SOURCE
#include <fcntl.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

#include "check.h"
#include "inroot.h"

static void normalizes_relative_paths(void)
{
	char out[256];
	CHECK(!normalize_rel("./etc//apt/./sources.list", out, sizeof out));
	CHECK_STR(out, "etc/apt/sources.list");
	CHECK(!normalize_rel("/usr/bin/", out, sizeof out));
	CHECK_STR(out, "usr/bin");
	CHECK(!normalize_rel("./", out, sizeof out));
	CHECK_STR(out, "");
	CHECK(normalize_rel("a/../b", out, sizeof out));
	CHECK(normalize_rel("..", out, sizeof out));
	CHECK(normalize_rel("abc", out, 3));
}

static void links_resolve_inside_the_root(void)
{
	char dir[] = "/tmp/pccroot-XXXXXX", cmd[512];
	CHECK(mkdtemp(dir) != NULL);
	snprintf(cmd, sizeof cmd,
		 "cd %s && mkdir -p root/usr/share outside && echo in > root/usr/share/f && echo out > outside/f && "
		 "ln -s /usr/share root/abs && ln -s ../../outside root/up && ln -s /proc/self/root root/magic",
		 dir);
	CHECK(!system(cmd));
	snprintf(cmd, sizeof cmd, "%s/root", dir);
	int root = open(cmd, O_DIRECTORY | O_RDONLY | O_CLOEXEC);
	char text[8] = { 0 };
	int fd = open_in_root(root, "abs/f", O_RDONLY, 0);
	CHECK(fd >= 0 && read(fd, text, 7) == 3);
	CHECK_STR(text, "in\n");
	close(fd);
	fd = open_in_root(root, "up/f", O_RDONLY, 0); /* ../../outside is clamped to the root: no file */
	CHECK(fd < 0);
	CHECK(open_in_root(root, "magic/etc/passwd", O_RDONLY, 0) < 0);

	/* up -> ../../outside means <root>/outside, which does not exist: nothing is made anywhere. */
	CHECK(make_dirs_in_root(root, "up/made/here", 0755, NULL, NULL));
	snprintf(cmd, sizeof cmd, "%s/outside/made", dir);
	struct stat st;
	CHECK(stat(cmd, &st));
	CHECK(!make_dirs_in_root(root, "abs/made/here", 0755, NULL, NULL));
	snprintf(cmd, sizeof cmd, "%s/root/usr/share/made/here", dir);
	CHECK(!stat(cmd, &st) && S_ISDIR(st.st_mode));
	close(root);
	snprintf(cmd, sizeof cmd, "rm -rf %s", dir);
	CHECK(!system(cmd));
}

int main(void)
{
	RUN(normalizes_relative_paths);
	RUN(links_resolve_inside_the_root);
	return CHECK_DONE();
}
