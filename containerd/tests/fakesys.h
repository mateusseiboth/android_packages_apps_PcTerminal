/* A fake machine for the host tests: the daemon's folders in a temporary tree, and a fake init that
 * "runs" by writing its pid into the container's cgroup.procs. */
#pragma once
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "sysops.h"

static char fake_root[64];
static char fake_paths[7][160];
static struct sys_env fake_env;

static struct {
	int starts, execs, signals, last_signal, rows, cols;
	char last_name[64], last_user[64], last_resolv[256], last_argv0[256], last_cwd[256], last_env[2048];
	unsigned last_id_base, last_uid, last_storage_uid;
	int last_plan_count, last_use_pty;
	int next_pid;
	int fail_exec;
} fake;

static inline const char *fake_path(const char *rel)
{
	static char out[8][256];
	static int next;
	char *p = out[next++ % 8];
	snprintf(p, 256, "%s/%s", fake_root, rel);
	return p;
}

static inline void fake_write(const char *path, const char *text)
{
	char cmd[2048];
	snprintf(cmd, sizeof cmd, "mkdir -p \"$(dirname '%s')\" && printf '%%s' '%s' > '%s'", path, text, path);
	if (system(cmd))
		fprintf(stderr, "fake_write %s failed\n", path);
}

static inline int fake_start_init(const struct sys_env *env, const struct start_spec *spec, int *pid)
{
	char path[512], text[64];
	fake.starts++;
	snprintf(fake.last_name, sizeof fake.last_name, "%s", spec->name);
	snprintf(fake.last_user, sizeof fake.last_user, "%s", spec->user);
	snprintf(fake.last_resolv, sizeof fake.last_resolv, "%s", spec->resolv_conf ? spec->resolv_conf : "(none)");
	fake.last_id_base = spec->id_base;
	fake.last_storage_uid = spec->storage_uid;
	fake.last_plan_count = spec->plan_count;
	*pid = 5000 + fake.starts;
	snprintf(path, sizeof path, "%s/cgroup.procs", spec->cgroup_dir);
	snprintf(text, sizeof text, "%d\n", *pid);
	fake_write(path, text);
	snprintf(path, sizeof path, "%s/%d/status", env->proc, *pid);
	snprintf(text, sizeof text, "NSpid:\t%d\t1\n", *pid);
	fake_write(path, text);
	return 0;
}

static inline int fake_exec_in(const struct sys_env *env, const struct exec_spec *spec, int *pty, int *pid, char *error, size_t cap)
{
	(void)env;
	fake.execs++;
	if (fake.fail_exec) {
		snprintf(error, cap, "No such file or directory");
		return -1;
	}
	snprintf(fake.last_argv0, sizeof fake.last_argv0, "%s", spec->argv[0]);
	snprintf(fake.last_cwd, sizeof fake.last_cwd, "%s", spec->cwd);
	fake.last_env[0] = 0;
	for (char *const *e = spec->envp; *e; e++)
		snprintf(fake.last_env + strlen(fake.last_env), sizeof fake.last_env - strlen(fake.last_env), "%s;", *e);
	fake.last_uid = spec->uid;
	fake.last_use_pty = spec->use_pty;
	*pty = spec->use_pty ? dup(0) : -1;
	*pid = 42;
	return 0;
}

/* A container's processes end on SIGTERM: its cgroup empties, as the kernel would show it. */
static inline int fake_send_signal(int pid, int sig)
{
	char cmd[512];
	fake.signals++;
	fake.last_signal = sig;
	snprintf(cmd, sizeof cmd, "grep -l '^%d$' %s/cgroup/pc-containers/*/cgroup.procs | xargs -r rm -f", pid, fake_root);
	return sig == SIGTERM && system(cmd) ? -1 : 0;
}

static inline int fake_chown_at(void *ctx, int dirfd, const char *name, unsigned uid, unsigned gid)
{
	(void)ctx, (void)dirfd, (void)name, (void)uid, (void)gid;
	return 0;
}

static inline int fake_set_window_size(int fd, int rows, int cols)
{
	(void)fd;
	fake.rows = rows;
	fake.cols = cols;
	return 0;
}

static inline void fake_sleep_ms(int ms) { (void)ms; }

static const struct sys_ops fake_ops = { fake_start_init, fake_exec_in, fake_send_signal, fake_chown_at,
					 fake_set_window_size, fake_sleep_ms };

static inline void fake_setup(void)
{
	static const char *const RELS[] = { "linux", "media", "wayland", "cgroup", "proc", "dev", "packages.list" };
	snprintf(fake_root, sizeof fake_root, "/tmp/pccfake-XXXXXX");
	if (!mkdtemp(fake_root))
		exit(1);
	for (int i = 0; i < 7; i++)
		snprintf(fake_paths[i], sizeof fake_paths[i], "%s/%s", fake_root, RELS[i]);
	fake_env = (struct sys_env){ fake_paths[0], fake_paths[1], fake_paths[2], fake_paths[3],
				     fake_paths[4], fake_paths[5], fake_paths[6], 0 };
	for (int i = 0; i < 6; i++)
		mkdir(fake_paths[i], 0755);
	fake_write(fake_paths[6], "com.android.providers.media.module 10161 0 /data/user/0/x platform:privapp 1023,1015\n");
	memset(&fake, 0, sizeof fake);
}

static inline void fake_cleanup(void)
{
	char cmd[128];
	snprintf(cmd, sizeof cmd, "rm -rf %s", fake_root);
	if (system(cmd))
		fprintf(stderr, "cleanup failed\n");
}
