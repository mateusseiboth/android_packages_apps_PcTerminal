/* A command run with exec_in is adopted and reaped by the container's init, and ends when the container
 * stops. Needs real root and cgroup2 (sudo); as a normal user it says so and passes. The container is a
 * real one: an empty rootfs whose /usr is the host's, bound read-only in its own mount namespace. */
#define _GNU_SOURCE
#include <signal.h>
#include <stdlib.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <unistd.h>

#include "check.h"
#include "sysops.h"
#include "util.h"

#define ID_BASE 2009900000u /* Android user 99 */

static char rootfs[64], cgroup_dir[128];
static struct mount_step plan[2];
static const struct sys_env env = { "", "", "", "/sys/fs/cgroup", "/proc", "/dev", "", 0 };

static int read_status_field(int pid, const char *field, char *out, size_t cap)
{
	char path[64], text[4096];
	snprintf(path, sizeof path, "/proc/%d/status", pid);
	if (read_text_file(path, text, sizeof text))
		return -1;
	const char *line = strstr(text, field);
	if (!line)
		return -1;
	line += strlen(field);
	line += strspn(line, ":\t ");
	snprintf(out, cap, "%.*s", (int)strcspn(line, "\t\n"), line);
	return 0;
}

static int is_gone(int pid)
{
	char state[64];
	for (int i = 0; i < 20 && !read_status_field(pid, "State", state, sizeof state); i++)
		usleep(50 * 1000);
	return read_status_field(pid, "State", state, sizeof state) != 0; /* a zombie still has a status */
}

/* The host pid of the group's process whose argv[0] is that path, -1 when none. */
static int find_host_pid(const char *argv0, int other_than)
{
	char path[256], text[4096], cmdline[256];
	snprintf(path, sizeof path, "%s/cgroup.procs", cgroup_dir);
	if (read_text_file(path, text, sizeof text))
		return -1;
	for (char *save = NULL, *tok = strtok_r(text, "\n", &save); tok; tok = strtok_r(NULL, "\n", &save)) {
		snprintf(path, sizeof path, "/proc/%s/cmdline", tok);
		if (!read_text_file(path, cmdline, sizeof cmdline) && !strcmp(cmdline, argv0) && atoi(tok) != other_than)
			return atoi(tok);
	}
	return -1;
}

/* As root, kill(-1) signals every process on the machine and kill(0) the test's own group: only a real
 * pid found in the container's cgroup may reach kill(). */
static int signal_container_process(int pid, int sig)
{
	return pid > 1 ? kill(pid, sig) : -1;
}

static int run_sleep(int init, int other_than)
{
	static char *argv[] = { "/usr/bin/sleep", "1000", NULL }, *envp[] = { "PATH=/usr/bin:/bin", NULL };
	char error[256];
	int pty = -1, pid = -1;
	struct exec_spec spec = { init, cgroup_dir, argv, envp, "/", 1000, 1000, 0, 0, 0, ID_BASE, 0 };
	CHECK(!linux_sys_ops()->exec_in(&env, &spec, &pty, &pid, error, sizeof error));
	CHECK(pid > 1); /* its pid inside the container */
	return find_host_pid("/usr/bin/sleep", other_than);
}

static void the_init_adopts_reaps_and_ends_commands(void)
{
	char cmd[256], ppid[32], uid[64], expected[64];
	snprintf(rootfs, sizeof rootfs, "/tmp/pcc-reaper-XXXXXX");
	CHECK(mkdtemp(rootfs) != NULL);
	snprintf(cmd, sizeof cmd, "cd %s && chmod 755 . && mkdir -p etc usr && ln -s usr/bin bin && ln -s usr/lib lib && ln -s usr/lib64 lib64",
		 rootfs);
	CHECK(!system(cmd));
	snprintf(cgroup_dir, sizeof cgroup_dir, "/sys/fs/cgroup/pcc-reaper-%d", (int)getpid());
	CHECK(!mkdir(cgroup_dir, 0755));
	plan[0] = (struct mount_step){ .kind = MOUNT_PROC, .source = "proc", .target = "/proc" };
	plan[1] = (struct mount_step){ .kind = MOUNT_RBIND, .source = "/usr", .target = "/usr" };
	struct start_spec spec = { "reaper", rootfs, cgroup_dir, "tester", ID_BASE, 0, plan, 2, NULL };
	int init = -1;
	CHECK(!linux_sys_ops()->start_init(&env, &spec, &init) && init > 0);

	int first = run_sleep(init, -1);
	CHECK(first > 0);
	if (first <= 0)
		return;
	snprintf(expected, sizeof expected, "%d", init);
	CHECK(!read_status_field(first, "PPid", ppid, sizeof ppid));
	CHECK_STR(ppid, expected); /* the container's init, not a host process */
	snprintf(expected, sizeof expected, "%u", ID_BASE + 1000);
	CHECK(!read_status_field(first, "Uid", uid, sizeof uid));
	CHECK_STR(uid, expected);
	signal_container_process(first, SIGKILL);
	CHECK(is_gone(first)); /* reaped, not left a zombie */

	int second = run_sleep(init, first);
	CHECK(second > 0);
	signal_container_process(init, SIGTERM); /* what stop does first */
	CHECK(is_gone(second));
	CHECK(is_gone(init));

	rmdir(cgroup_dir);
	snprintf(cmd, sizeof cmd, "rm -rf %s", rootfs);
	CHECK(!system(cmd));
}

int main(void)
{
	if (geteuid()) {
		printf("skip the_init_adopts_reaps_and_ends_commands (needs root)\n");
		return 0;
	}
	RUN(the_init_adopts_reaps_and_ends_commands);
	return CHECK_DONE();
}
