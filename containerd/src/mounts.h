/*
 * What a container gets mounted before pivot_root, in order (README, "Sharing"). Targets are paths inside
 * the container; sources are host paths. sysops_linux.c carries the plan out.
 */
#pragma once

#define MOUNT_STEPS_MAX 32

enum mount_kind {
	MOUNT_PROC,    /* a proc of the container's pid namespace */
	MOUNT_SYSFS,   /* the host's /sys, read-only */
	MOUNT_CGROUP2, /* cgroup2 seen from the container's cgroup namespace */
	MOUNT_TMPFS,
	MOUNT_DEVPTS,  /* a new devpts instance: the container's own ptys */
	MOUNT_MQUEUE,
	MOUNT_BIND,
	MOUNT_RBIND,
	MOUNT_SYMLINK, /* source is the link's text */
	MOUNT_STORAGE, /* the person's storage, id-mapped to the container's user when the kernel can */
};

struct mount_step {
	enum mount_kind kind;
	char source[256];
	char target[128];
	char options[96];
	unsigned long flags;
	int is_optional; /* skipped when the source is missing */
	int is_file;     /* the target is a file (a device node) */
};

struct plan_input {
	const char *dev;          /* /dev */
	const char *media_root;   /* /data/media */
	const char *wayland_root; /* /data/misc/pc-wayland */
	unsigned user_id;
	const char *user;
};

/* The number of steps, or -1 when they do not fit in max. */
int build_mount_plan(const struct plan_input *in, struct mount_step *steps, int max);
