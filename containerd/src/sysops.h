/*
 * Where pc-containerd looks (sys_env) and the privileged calls it makes (sys_ops). The device uses the real
 * paths and linux_sys_ops(); the host tests point the paths at a temporary tree and use a fake.
 */
#pragma once
#include <stddef.h>

#include "mounts.h"

struct sys_env {
	const char *data_root;     /* /data/linux */
	const char *media_root;    /* /data/media */
	const char *wayland_root;  /* /data/misc/pc-wayland */
	const char *cgroup_root;   /* /sys/fs/cgroup */
	const char *proc;          /* /proc */
	const char *dev;           /* /dev */
	const char *packages_list; /* /data/system/packages.list */
	/* Host trials as a normal user (README, "Host trials"): only container id 0 is mapped, to the
	 * daemon's own uid, and nothing needs root. */
	int is_rootless;
	const char *x11_bundle;    /* /system_ext/pc-x11: X11 and dbus for distros without them */
};

struct start_spec {
	const char *name;       /* also the host name */
	const char *rootfs;     /* host path */
	const char *cgroup_dir; /* made by the caller */
	const char *user;       /* the person's account inside */
	unsigned id_base;       /* host id of container id 0 */
	unsigned storage_uid;   /* host owner of the person's files (MediaProvider); 0 when unknown */
	const struct mount_step *plan;
	int plan_count;
	const char *resolv_conf; /* text for /etc/resolv.conf, or NULL to leave it */
};

struct exec_spec {
	int init_pid;           /* host pid of the container's init */
	const char *cgroup_dir;
	char *const *argv;      /* argv[0] without '/' is looked up in the PATH of envp */
	char *const *envp;
	const char *cwd;
	unsigned uid, gid;      /* inside the container */
	int use_pty, rows, cols;
	unsigned id_base;       /* host id of container id 0 */
	int is_rootless;
};

struct sys_ops {
	/* Starts the container's init (namespaces, mounts, pivot_root, cgroup) and waits until it is ready. */
	int (*start_init)(const struct sys_env *env, const struct start_spec *spec, int *pid);
	/* Runs a command in the container; *pty is the master (or -1), *pid its pid inside the container. */
	int (*exec_in)(const struct sys_env *env, const struct exec_spec *spec, int *pty, int *pid, char *error, size_t cap);
	int (*send_signal)(int pid, int sig);
	/* lchown of name in dirfd ("" is dirfd itself); ctx is unused by the device. */
	int (*chown_at)(void *ctx, int dirfd, const char *name, unsigned uid, unsigned gid);
	int (*set_window_size)(int fd, int rows, int cols);
	void (*sleep_ms)(int ms);
};

const struct sys_env *linux_sys_env(void);
const struct sys_ops *linux_sys_ops(void);
/* Host trials: files keep the daemon user as owner. */
const struct sys_ops *linux_rootless_sys_ops(void);/* A user namespace that only maps (inside -> outside), for id-mapped mounts. Its fd, or -1. */
int make_idmap_userns(unsigned uid_inside, unsigned gid_inside, unsigned outside);

/* Binds rootfs over itself with setuid allowed (sudo, su), as /data is nosuid. Run in a mount namespace
 * of its own as real root, before the user namespace, which then locks the mount that way. */
int allow_setuid(const char *rootfs);


