/* What a container gets mounted, in order: its own /proc, /dev and /run, the host's /sys, GPU and sound
 * devices, the Wayland folder and the person's storage as their home. */
#include "check.h"
#include "mounts.h"

static struct mount_step steps[MOUNT_STEPS_MAX];
static int count;

static int find_step(const char *target)
{
	for (int i = 0; i < count; i++)
		if (!strcmp(steps[i].target, target))
			return i;
	return -1;
}

static void builds_the_plan_for_one_user(void)
{
	struct plan_input in = { "/dev", "/data/media", "/data/misc/pc-wayland", 10, "mateus" };
	count = build_mount_plan(&in, steps, MOUNT_STEPS_MAX);
	CHECK(count > 10);
	int home = find_step("/home/mateus");
	CHECK(home >= 0 && steps[home].kind == MOUNT_STORAGE && !strcmp(steps[home].source, "/data/media/10"));
	int wayland = find_step("/run/pc-wayland");
	CHECK(wayland >= 0 && steps[wayland].kind == MOUNT_BIND && !strcmp(steps[wayland].source, "/data/misc/pc-wayland/10"));
	CHECK(find_step("/run") >= 0 && find_step("/run") < wayland);
	int dri = find_step("/dev/dri");
	CHECK(dri >= 0 && steps[dri].kind == MOUNT_RBIND && steps[dri].is_optional && !strcmp(steps[dri].source, "/dev/dri"));
	int snd = find_step("/dev/snd");
	CHECK(snd >= 0 && steps[snd].is_optional);
	int null = find_step("/dev/null");
	CHECK(null >= 0 && steps[null].kind == MOUNT_BIND && steps[null].is_file && !steps[null].is_optional);
	int dev = find_step("/dev"), pts = find_step("/dev/pts");
	CHECK(dev >= 0 && dev < null && dev < pts && steps[dev].kind == MOUNT_TMPFS);
	CHECK(pts >= 0 && steps[pts].kind == MOUNT_DEVPTS && strstr(steps[pts].options, "newinstance"));
	int ptmx = find_step("/dev/ptmx");
	CHECK(ptmx >= 0 && steps[ptmx].kind == MOUNT_SYMLINK && !strcmp(steps[ptmx].source, "pts/ptmx"));
	int proc = find_step("/proc"), sys = find_step("/sys");
	CHECK(proc >= 0 && steps[proc].kind == MOUNT_PROC);
	CHECK(sys >= 0 && steps[sys].kind == MOUNT_SYSFS);
	int cgroup = find_step("/sys/fs/cgroup"); /* the container's own group as the root */
	CHECK(cgroup > sys && steps[cgroup].kind == MOUNT_CGROUP2 && steps[cgroup].is_optional);
	CHECK(find_step("/dev/shm") > dev);
}

static void a_small_table_is_never_overrun(void)
{
	struct plan_input in = { "/dev", "/data/media", "/data/misc/pc-wayland", 0, "user" };
	CHECK(build_mount_plan(&in, steps, 3) == -1);
}

int main(void)
{
	RUN(builds_the_plan_for_one_user);
	RUN(a_small_table_is_never_overrun);
	return CHECK_DONE();
}
