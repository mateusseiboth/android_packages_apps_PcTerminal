/* /data is nosuid on Android, so sudo and su inside a container need the rootfs mounted with setuid
 * allowed. Needs real root (sudo make test); as a normal user it says so and passes. */
#define _GNU_SOURCE
#include <sched.h>
#include <stdlib.h>
#include <sys/mount.h>
#include <sys/statvfs.h>
#include <sys/wait.h>
#include <unistd.h>

#include "check.h"
#include "sysops.h"

static int has_nosuid(const char *path)
{
	struct statvfs st;
	return statvfs(path, &st) || (st.f_flag & ST_NOSUID);
}

static void the_rootfs_allows_setuid_on_a_nosuid_data(void)
{
	char data[] = "/tmp/pcc-setuid-XXXXXX";
	CHECK(mkdtemp(data) != NULL);
	CHECK(!mount("tmpfs", data, "tmpfs", MS_NOSUID | MS_NODEV, "mode=0755"));
	CHECK(has_nosuid(data));
	pid_t child = fork();
	if (child == 0)
		_exit(unshare(CLONE_NEWNS) || mount(NULL, "/", NULL, MS_REC | MS_PRIVATE, NULL) ||
		      allow_setuid(data) || has_nosuid(data));
	int status = -1;
	waitpid(child, &status, 0);
	CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 0);
	CHECK(has_nosuid(data)); /* the host's own mount is left alone */
	umount(data);
	rmdir(data);
}

int main(void)
{
	if (geteuid()) {
		printf("skip the_rootfs_allows_setuid_on_a_nosuid_data (needs root)\n");
		return 0;
	}
	RUN(the_rootfs_allows_setuid_on_a_nosuid_data);
	return CHECK_DONE();
}
