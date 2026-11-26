/*
 * The person's session inside a running container (README, "Session"): a D-Bus session bus, X11 through
 * xwayland-satellite and PipeWire, whose default sink is the compositor's pipe /run/pc-wayland/audio-out.
 * Each piece runs as the container's user, started with exec_in, so it lands in the container's pid
 * namespace: the container's init adopts and reaps it, and it ends with the container. A piece starts only
 * when its programs are in the rootfs and it is not running yet, so the daemon calls start_session at every
 * start and exec and only what is missing starts.
 */
#pragma once

#include "sysops.h"

struct session_target {
	const struct sys_env *env;
	const struct sys_ops *ops;
	int root_fd;            /* the container's rootfs, opened by the daemon */
	const char *cgroup_dir; /* the container's group, to see what runs */
	int init_pid;           /* host pid of the container's init */
	unsigned id_base;       /* host id of container id 0: owner of the files written */
	char *const *envp;      /* the user's exec environment (runtime.h) */
	const char *home;
};

/* The number of pieces started. Missing programs and failed starts are skipped, never an error. */
int start_session(const struct session_target *t);
