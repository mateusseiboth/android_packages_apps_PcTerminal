/* A container's cgroup: whether it runs, its init and its numbers. */
#include <stdlib.h>

#include "check.h"
#include "cgroup.h"

static char root[64];

static void write_file(const char *rel, const char *text)
{
	char cmd[512];
	snprintf(cmd, sizeof cmd, "mkdir -p \"$(dirname %s/%s)\" && printf '%%s' '%s' > %s/%s", root, rel, text, root, rel);
	CHECK(!system(cmd));
}

static void names_the_group_after_user_and_container(void)
{
	char path[256];
	build_cgroup_dir("/sys/fs/cgroup", 10, "debian", path, sizeof path);
	CHECK_STR(path, "/sys/fs/cgroup/pc-containers/u10-debian");
}

static void reads_cpu_usage_from_cpu_stat(void)
{
	unsigned long long usec = 0;
	CHECK(!parse_cpu_usage("usage_usec 1834000\nuser_usec 1200000\nsystem_usec 634000\n", &usec));
	CHECK(usec == 1834000);
	CHECK(parse_cpu_usage("user_usec 5\n", &usec));
}

static void finds_the_init_among_the_group_processes(void)
{
	char cg[128], proc[128];
	snprintf(root, sizeof root, "/tmp/pcccg-XXXXXX");
	CHECK(mkdtemp(root) != NULL);
	snprintf(cg, sizeof cg, "%s/cg", root);
	snprintf(proc, sizeof proc, "%s/proc", root);
	write_file("cg/cgroup.procs", "");
	CHECK(find_init_pid(cg, proc) == -1);
	write_file("cg/cgroup.procs", "4100\n4000\n4200\n");
	write_file("proc/4100/status", "Name:\tbash\nNSpid:\t4100\t7\n");
	write_file("proc/4000/status", "Name:\tpc-containerd\nPid:\t4000\nNSpid:\t4000\t1\nUid:\t0\n");
	write_file("proc/4200/status", "Name:\tsleep\nNSpid:\t4200\t9\n");
	CHECK(find_init_pid(cg, proc) == 4000);
	CHECK(find_ns_pid(proc, 4100) == 7);
}

static void reads_the_numbers_that_exist(void)
{
	char cg[128];
	struct cgroup_stats s;
	snprintf(cg, sizeof cg, "%s/cg", root);
	write_file("cg/memory.current", "73400320\n");
	write_file("cg/cpu.stat", "usage_usec 99\n");
	read_cgroup_stats(cg, &s);
	CHECK(s.has_memory && s.memory_bytes == 73400320);
	CHECK(s.has_cpu && s.cpu_usec == 99);
	CHECK(s.has_pids && s.pids == 3); /* no pids.current: the processes are counted */
	write_file("cg/pids.current", "5\n");
	read_cgroup_stats(cg, &s);
	CHECK(s.pids == 5);
	char missing[160];
	snprintf(missing, sizeof missing, "%s/none", root);
	read_cgroup_stats(missing, &s);
	CHECK(!s.has_memory && !s.has_cpu && !s.has_pids);
}

static void counts_disk_usage_once_per_file(void)
{
	char cmd[1024], dir[160];
	snprintf(dir, sizeof dir, "%s/rootfs", root);
	snprintf(cmd, sizeof cmd,
		 "mkdir -p %s/a && head -c 100000 /dev/zero > %s/a/f && ln %s/a/f %s/hard && ln -s /usr %s/link", dir, dir, dir,
		 dir, dir);
	CHECK(!system(cmd));
	unsigned long long bytes = find_disk_usage(dir);
	CHECK(bytes >= 100000 && bytes < 160000);
	snprintf(cmd, sizeof cmd, "rm -rf %s", root);
	CHECK(!system(cmd));
}

int main(void)
{
	RUN(names_the_group_after_user_and_container);
	RUN(reads_cpu_usage_from_cpu_stat);
	RUN(finds_the_init_among_the_group_processes);
	RUN(reads_the_numbers_that_exist);
	RUN(counts_disk_usage_once_per_file);
	return CHECK_DONE();
}
