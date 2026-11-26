/*
 * The Linux side of a container (README, "Design"): its init in new user, mount, pid, UTS, IPC and cgroup
 * namespaces (the network is the host's), the mount plan carried out below the rootfs, pivot_root, and
 * commands entered with setns. Host trials as a normal user (--rootless) map one id and skip what needs
 * root; the host tests cover the rest through a fake (tests/fakesys.h).
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <grp.h>
#include <limits.h>
#include <linux/mount.h>
#include <sched.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mount.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#include "accounts.h"
#include "cgroup.h"
#include "container_files.h"
#include "ids.h"
#include "inroot.h"
#include "sysops.h"
#include "util.h"

#ifndef SYS_pivot_root
#define SYS_pivot_root 155
#endif
#ifndef SYS_open_tree
#define SYS_open_tree 428
#endif
#ifndef SYS_move_mount
#define SYS_move_mount 429
#endif
#ifndef SYS_mount_setattr
#define SYS_mount_setattr 442
#endif
#ifndef SYS_close_range
#define SYS_close_range 436
#endif
#ifndef TIOCGPTPEER
#define TIOCGPTPEER _IO('T', 0x41)
#endif

#define AID_MEDIA_RW 1023u
#define TTY_GID 5u
#ifndef CLOSE_RANGE_CLOEXEC
#define CLOSE_RANGE_CLOEXEC 4
#endif
#define STOP_GRACE_SECONDS 5
#define ERROR_MAX 200

/* ---- small helpers ---------------------------------------------------------------------- */

static int write_all(int fd, const void *buf, size_t len)
{
	for (size_t done = 0; done < len;) {
		ssize_t n = write(fd, (const char *)buf + done, len - done);
		if (n < 0 && errno == EINTR)
			continue;
		if (n <= 0)
			return -1;
		done += (size_t)n;
	}
	return 0;
}

static int read_all(int fd, void *buf, size_t len)
{
	for (size_t done = 0; done < len;) {
		ssize_t n = read(fd, (char *)buf + done, len - done);
		if (n < 0 && errno == EINTR)
			continue;
		if (n <= 0)
			return -1;
		done += (size_t)n;
	}
	return 0;
}

static int write_proc_file(int pid, const char *leaf, const char *text)
{
	char path[64];
	snprintf(path, sizeof path, "/proc/%d/%s", pid, leaf);
	int fd = open(path, O_WRONLY | O_CLOEXEC);
	int failed = fd < 0 || write_all(fd, text, strlen(text));
	if (fd >= 0)
		close(fd);
	return failed ? -1 : 0;
}

/* Maps container ids 0..65535 to the user's host range; a host trial maps only id 0, to itself. */
static int write_id_maps(const struct sys_env *env, int pid, unsigned base)
{
	char uid_map[64], gid_map[64];
	snprintf(uid_map, sizeof uid_map, "0 %u %u\n", env->is_rootless ? getuid() : base, env->is_rootless ? 1 : CONTAINER_ID_COUNT);
	snprintf(gid_map, sizeof gid_map, "0 %u %u\n", env->is_rootless ? getgid() : base, env->is_rootless ? 1 : CONTAINER_ID_COUNT);
	if (env->is_rootless && write_proc_file(pid, "setgroups", "deny"))
		return -1;
	return write_proc_file(pid, "uid_map", uid_map) || write_proc_file(pid, "gid_map", gid_map) ? -1 : 0;
}

static void report_error(int fd, const char *what)
{
	char text[ERROR_MAX];
	snprintf(text, sizeof text, "%s: %s", what, strerror(errno));
	write_all(fd, text, strlen(text));
}

static void __attribute__((noreturn)) fail_exit(int status, const char *what, int code)
{
	report_error(status, what);
	_exit(code);
}

/* ---- the person's storage, id-mapped ---------------------------------------------------- */

/* A user namespace that only maps (inside -> outside): uid and gid apart. Its fd, or -1. */
int make_idmap_userns(unsigned uid_inside, unsigned gid_inside, unsigned outside)
{
	int ready[2], hold[2];
	if (pipe2(ready, O_CLOEXEC) || pipe2(hold, O_CLOEXEC))
		return -1;
	pid_t pid = fork();
	if (pid == 0) {
		/* Its own copy of hold's write end would keep the read below from ever seeing EOF. */
		close(hold[1]);
		close(ready[0]);
		char c = unshare(CLONE_NEWUSER) ? 'E' : 'K';
		write_all(ready[1], &c, 1);
		read(hold[0], &c, 1);
		_exit(0);
	}
	char c = 0, map[64], path[64];
	int fd = -1;
	if (pid > 0 && !read_all(ready[0], &c, 1) && c == 'K') {
		snprintf(map, sizeof map, "%u %u 1\n", uid_inside, outside);
		int failed = write_proc_file(pid, "uid_map", map);
		snprintf(map, sizeof map, "%u %u 1\n", gid_inside, outside);
		failed = failed || write_proc_file(pid, "gid_map", map);
		snprintf(path, sizeof path, "/proc/%d/ns/user", pid);
		fd = failed ? -1 : open(path, O_RDONLY | O_CLOEXEC);
	}
	close(hold[1]);
	if (pid > 0)
		waitpid(pid, NULL, 0);
	close(ready[0]), close(ready[1]), close(hold[0]);
	return fd;
}

/* A detached copy of the storage folder where MediaProvider's files (and media_rw's group) belong to
 * the container's user. -1 when the kernel or the file system cannot: the init then binds it as is. */
static int make_storage_mount(const struct sys_env *env, const struct start_spec *spec)
{
	const struct mount_step *home = NULL;
	for (int i = 0; i < spec->plan_count; i++)
		home = spec->plan[i].kind == MOUNT_STORAGE ? &spec->plan[i] : home;
	if (env->is_rootless || !spec->storage_uid || !home)
		return -1;
	int tree = (int)syscall(SYS_open_tree, AT_FDCWD, home->source, OPEN_TREE_CLONE | OPEN_TREE_CLOEXEC | AT_RECURSIVE);
	int userns = tree >= 0 ? make_idmap_userns(spec->storage_uid, AID_MEDIA_RW, spec->id_base + CONTAINER_USER_ID) : -1;
	struct mount_attr attr = { .attr_set = MOUNT_ATTR_IDMAP, .userns_fd = (unsigned)userns };
	int failed = userns < 0 || syscall(SYS_mount_setattr, tree, "", AT_EMPTY_PATH | AT_RECURSIVE, &attr, sizeof attr);
	if (userns >= 0)
		close(userns);
	if (failed && tree >= 0) {
		close(tree);
		return -1;
	}
	return tree;
}

/* ---- carrying out the mount plan (inside the new mount namespace) ---------------------- */

struct setup {
	const struct sys_env *env;
	const struct start_spec *spec;
	int root_fd;    /* the rootfs, bind-mounted on itself */
	int storage_fd; /* make_storage_mount's, or -1 */
	int has_home;
};

/* Steps whose source is a host path, which may be missing (no GPU, no sound card). */
static int is_missing_host_source(const struct mount_step *s)
{
	static const enum mount_kind HOST_SOURCED[] = { MOUNT_BIND, MOUNT_RBIND, MOUNT_STORAGE, MOUNT_SYSFS };
	struct stat st;
	for (size_t i = 0; i < sizeof HOST_SOURCED / sizeof *HOST_SOURCED; i++)
		if (s->kind == HOST_SOURCED[i])
			return stat(s->source, &st) != 0;
	return 0;
}

/* The target made inside the root (a folder, or an empty file for a device node), as /proc/self/fd/N. */
static int open_target(struct setup *u, const struct mount_step *s, char *path, size_t cap)
{
	char base[NAME_MAX + 1];
	int dir = open_parent_in_root(u->root_fd, s->target, base, sizeof base, NULL, NULL);
	if (dir < 0)
		return -1;
	int made = s->is_file ? openat(dir, base, O_WRONLY | O_CREAT | O_NOFOLLOW | O_CLOEXEC, 0644) : mkdirat(dir, base, 0755);
	if (s->is_file && made >= 0)
		close(made);
	close(dir);
	int fd = open_in_root(u->root_fd, s->target, O_PATH | O_NOFOLLOW, 0);
	snprintf(path, cap, "/proc/self/fd/%d", fd);
	return fd;
}

static int mount_fs(const char *type, const char *path, const struct mount_step *s)
{
	return mount(type, path, type, s->flags, *s->options ? s->options : NULL);
}

/* devpts with gid=5 needs that gid mapped: a host trial maps only 0. */
static int mount_devpts(const char *path, const struct mount_step *s)
{
	return mount_fs("devpts", path, s) && mount("devpts", path, "devpts", s->flags, "newinstance,ptmxmode=0666,mode=0620");
}

/* The host's /sys, read-only when the kernel lets a user namespace add that flag. */
static int mount_sysfs(const char *path, const struct mount_step *s)
{
	if (mount(s->source, path, NULL, MS_BIND | MS_REC, NULL))
		return -1;
	mount(NULL, path, NULL, MS_BIND | MS_REMOUNT | MS_RDONLY | MS_NOSUID | MS_NODEV | MS_NOEXEC, NULL);
	return 0;
}

static int mount_storage(struct setup *u, const char *path, const struct mount_step *s)
{
	u->has_home = 1;
	if (u->storage_fd >= 0 && !syscall(SYS_move_mount, u->storage_fd, "", AT_FDCWD, path, MOVE_MOUNT_F_EMPTY_PATH))
		return 0;
	return mount(s->source, path, NULL, MS_BIND | MS_REC, NULL);
}

static int make_symlink(struct setup *u, const struct mount_step *s)
{
	char base[NAME_MAX + 1];
	int dir = open_parent_in_root(u->root_fd, s->target, base, sizeof base, NULL, NULL);
	if (dir < 0)
		return -1;
	unlinkat(dir, base, 0);
	int failed = symlinkat(s->source, dir, base);
	close(dir);
	return failed;
}

static int run_step(struct setup *u, const struct mount_step *s)
{
	char path[64];
	if (s->kind == MOUNT_SYMLINK)
		return make_symlink(u, s);
	if (s->is_optional && is_missing_host_source(s))
		return 0;
	int fd = open_target(u, s, path, sizeof path), failed = fd < 0;
	switch (failed ? -1 : (int)s->kind) {
	case MOUNT_PROC: failed = mount_fs("proc", path, s); break;
	case MOUNT_SYSFS: failed = mount_sysfs(path, s); break;
	case MOUNT_CGROUP2: failed = mount_fs("cgroup2", path, s); break;
	case MOUNT_TMPFS: failed = mount_fs("tmpfs", path, s); break;
	case MOUNT_DEVPTS: failed = mount_devpts(path, s); break;
	case MOUNT_MQUEUE: failed = mount_fs("mqueue", path, s); break;
	case MOUNT_BIND: failed = mount(s->source, path, NULL, MS_BIND, NULL); break;
	case MOUNT_RBIND: failed = mount(s->source, path, NULL, MS_BIND | MS_REC, NULL); break;
	case MOUNT_STORAGE: failed = mount_storage(u, path, s); break;
	default: break;
	}
	if (fd >= 0)
		close(fd);
	return failed && !s->is_optional ? -1 : 0;
}

/* pivot_root onto the current folder; a root that cannot be pivoted (an initramfs) is moved over instead. */
static int switch_root(void)
{
	if (!syscall(SYS_pivot_root, ".", "."))
		return umount2(".", MNT_DETACH) || chdir("/");
	return mount(".", "/", NULL, MS_MOVE, NULL) || chroot(".") || chdir("/");
}

static int enter_root(struct setup *u, int status)
{
	if (mount(NULL, "/", NULL, MS_REC | MS_PRIVATE, NULL) || mount(u->spec->rootfs, u->spec->rootfs, NULL, MS_BIND | MS_REC, NULL))
		return report_error(status, "rootfs"), -1;
	u->root_fd = open(u->spec->rootfs, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
	if (u->root_fd < 0)
		return report_error(status, "rootfs"), -1;
	for (int i = 0; i < u->spec->plan_count; i++)
		if (run_step(u, &u->spec->plan[i]))
			return report_error(status, u->spec->plan[i].target), -1;
	if (fchdir(u->root_fd) || switch_root())
		return report_error(status, "pivot_root"), -1;
	return 0;
}

/* After pivot_root: names, the account and, without the storage, a home of its own. */
static void write_inside(struct setup *u)
{
	char home[128];
	struct container_files f = { "", u->spec->name, u->spec->user, u->spec->resolv_conf };
	sethostname(u->spec->name, strlen(u->spec->name));
	write_container_files(&f);
	snprintf(home, sizeof home, "/home/%s", u->spec->user);
	if (!u->has_home && !mkdir(home, 0755) && chown(home, CONTAINER_USER_ID, CONTAINER_USER_ID))
		errno = 0;
}

/* ---- the init: reaps orphans, ends everything on SIGTERM -------------------------------- */

static void reap(void)
{
	while (waitpid(-1, NULL, WNOHANG) > 0)
		;
}

static void end_all(void)
{
	time_t until = time(NULL) + STOP_GRACE_SECONDS;
	kill(-1, SIGTERM);
	while (time(NULL) < until) {
		struct timespec step = { 0, 100 * 1000 * 1000 };
		if (waitpid(-1, NULL, WNOHANG) < 0 && errno == ECHILD)
			break;
		nanosleep(&step, NULL);
	}
	_exit(0); /* the kernel kills what is left in the pid namespace */
}

static void on_signal(int sig) { (void)sig; }

static void run_init(void)
{
	static const int HANDLED[] = { SIGTERM, SIGINT, SIGCHLD };
	struct sigaction sa = { .sa_handler = on_signal };
	sigset_t all;
	/* A pid namespace's init gets only the signals it handles; these are then taken by sigwaitinfo. */
	for (size_t i = 0; i < sizeof HANDLED / sizeof *HANDLED; i++)
		sigaction(HANDLED[i], &sa, NULL);
	sigfillset(&all);
	sigprocmask(SIG_SETMASK, &all, NULL);
	prctl(PR_SET_NAME, "pc-init");
	for (;;) {
		siginfo_t info;
		int sig = sigwaitinfo(&all, &info);
		if (sig == SIGTERM || sig == SIGINT)
			end_all();
		reap();
	}
}

/* pid 1 of the container: sets it up, says so on status, then becomes the init. */
static void run_container(const struct sys_env *env, const struct start_spec *spec, int storage_fd, int status)
{
	struct setup u = { env, spec, -1, storage_fd, 0 };
	if (enter_root(&u, status))
		_exit(1);
	write_inside(&u);
	write_all(status, "OK", 2);
	syscall(SYS_close_range, 0, ~0U, 0);
	int null = open("/dev/null", O_RDWR);
	for (int fd = 0; null >= 0 && fd < 3; fd++)
		dup2(null, fd);
	setsid();
	run_init();
}

/* The first child: joins the cgroup's namespaces after the parent moved it, then forks pid 1. */
static void run_namespace_child(const struct sys_env *env, const struct start_spec *spec, int storage_fd, int to_child,
				int from_child, int status)
{
	char c;
	int flags = CLONE_NEWUSER | CLONE_NEWNS | CLONE_NEWPID | CLONE_NEWUTS | CLONE_NEWIPC | CLONE_NEWCGROUP;
	if (read_all(to_child, &c, 1) || unshare(flags))
		fail_exit(status, "unshare", 1);
	c = 'U';
	if (write_all(from_child, &c, 1) || read_all(to_child, &c, 1) || c != 'M')
		_exit(1);
	if (setresgid(0, 0, 0) || setresuid(0, 0, 0))
		fail_exit(status, "setuid", 1);
	if (setgroups(0, NULL))
		errno = 0; /* denied in host trials */
	pid_t init = fork();
	if (init == 0)
		run_container(env, spec, storage_fd, status);
	write_all(from_child, &init, sizeof init);
	_exit(init < 0);
}

static int join_cgroup(const struct sys_env *env, const char *cgroup_dir, int pid)
{
	char path[PATH_MAX], text[32];
	snprintf(path, sizeof path, "%s/cgroup.procs", cgroup_dir);
	snprintf(text, sizeof text, "%d\n", pid);
	/* Host trials: a plain folder stands in for the cgroup, and the file just keeps the init's pid. */
	int fd = open(path, O_WRONLY | O_CLOEXEC | (env->is_rootless ? O_CREAT | O_TRUNC : 0), 0644);
	int failed = fd < 0 || write_all(fd, text, strlen(text));
	if (fd >= 0)
		close(fd);
	return failed ? -1 : 0;
}

/* Reads "OK" or an error text the container's pid 1 wrote before becoming the init. */
static int read_status(int fd)
{
	char text[ERROR_MAX + 1] = { 0 };
	ssize_t n = read(fd, text, ERROR_MAX);
	if (n == 2 && !memcmp(text, "OK", 2))
		return 0;
	fprintf(stderr, "pc-containerd: start failed: %s\n", n > 0 ? text : "no answer");
	return -1;
}

static int linux_start_init(const struct sys_env *env, const struct start_spec *spec, int *pid)
{
	int to_child[2], from_child[2], status[2];
	if (pipe2(to_child, O_CLOEXEC) || pipe2(from_child, O_CLOEXEC) || pipe2(status, O_CLOEXEC))
		return -1;
	int storage_fd = make_storage_mount(env, spec);
	pid_t child = fork();
	if (child == 0) {
		/* The parent's ends: kept open here, a failing parent would leave the child waiting forever. */
		close(to_child[1]), close(from_child[0]), close(status[0]);
		run_namespace_child(env, spec, storage_fd, to_child[0], from_child[1], status[1]);
	}
	close(to_child[0]), close(from_child[1]), close(status[1]);
	if (storage_fd >= 0)
		close(storage_fd);
	char c = 'G';
	pid_t init = -1;
	int failed = child < 0 || join_cgroup(env, spec->cgroup_dir, child) || write_all(to_child[1], &c, 1) ||
		     read_all(from_child[0], &c, 1) || write_id_maps(env, child, spec->id_base);
	c = 'M';
	failed = failed || write_all(to_child[1], &c, 1) || read_all(from_child[0], &init, sizeof init) || init <= 0;
	close(to_child[1]), close(from_child[0]);
	if (child > 0)
		waitpid(child, NULL, 0);
	failed = failed || read_status(status[0]) || (env->is_rootless && join_cgroup(env, spec->cgroup_dir, init));
	close(status[0]);
	*pid = init;
	return failed ? -1 : 0;
}

/* ---- exec ------------------------------------------------------------------------------- */

static const char *const NAMESPACES[] = { "user", "mnt", "pid", "uts", "ipc", "cgroup" };
#define NS_COUNT (sizeof NAMESPACES / sizeof *NAMESPACES)

static int open_namespaces(int pid, int *fds)
{
	char path[64];
	for (size_t i = 0; i < NS_COUNT; i++) {
		snprintf(path, sizeof path, "/proc/%d/ns/%s", pid, NAMESPACES[i]);
		fds[i] = open(path, O_RDONLY | O_CLOEXEC);
		if (fds[i] < 0)
			return -1;
	}
	return 0;
}

/* The container's own devpts: the pty number is the one programs inside see. */
static int open_pty(const struct exec_spec *spec, int *slave)
{
	char path[64];
	snprintf(path, sizeof path, "/proc/%d/root/dev/pts/ptmx", spec->init_pid);
	int master = open(path, O_RDWR | O_NOCTTY | O_CLOEXEC);
	int unlock = 0;
	struct winsize ws = { (unsigned short)spec->rows, (unsigned short)spec->cols, 0, 0 };
	if (master < 0 || ioctl(master, TIOCSPTLCK, &unlock) || ioctl(master, TIOCSWINSZ, &ws))
		return master >= 0 ? (close(master), -1) : -1;
	*slave = ioctl(master, TIOCGPTPEER, O_RDWR | O_NOCTTY | O_CLOEXEC);
	if (*slave < 0) {
		close(master);
		return -1;
	}
	/* The terminal belongs to the command's user and the container's tty group, as login would leave it. */
	if (!spec->is_rootless && fchown(*slave, spec->id_base + spec->uid, spec->id_base + TTY_GID))
		errno = 0;
	return master;
}

/* execve over the PATH of envp, like a shell would. */
static void exec_command(char *const *argv, char *const *envp)
{
	const char *path = "/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin";
	for (char *const *e = envp; *e; e++)
		path = !strncmp(*e, "PATH=", 5) ? *e + 5 : path;
	if (strchr(argv[0], '/')) {
		execve(argv[0], argv, envp);
		return;
	}
	int saved = ENOENT;
	for (const char *dir = path; *dir;) {
		size_t len = strcspn(dir, ":");
		char full[PATH_MAX];
		snprintf(full, sizeof full, "%.*s/%s", (int)len, len ? dir : ".", argv[0]);
		execve(full, argv, envp);
		saved = errno == ENOENT || errno == ENOTDIR ? saved : errno;
		dir += len + (dir[len] == ':');
	}
	errno = saved;
}

static void set_groups(const struct exec_spec *spec)
{
	static char text[1 << 16];
	unsigned gids[32];
	int n = 0;
	char name[64] = "";
	if (spec->uid && !read_text_file("/etc/passwd", text, sizeof text))
		find_user_with_uid(text, spec->uid, name, sizeof name);
	if (*name && !read_text_file("/etc/group", text, sizeof text))
		n = find_member_gids(text, name, gids, 32);
	gid_t list[33];
	list[0] = spec->gid;
	for (int i = 0; i < n; i++)
		list[i + 1] = gids[i];
	if (setgroups((size_t)n + 1, list))
		errno = 0; /* denied in host trials */
}

/* The command's process, already inside every namespace: terminal, ids, folder, then exec. */
static void run_command(const struct exec_spec *spec, int slave, int status)
{
	sigset_t none;
	sigemptyset(&none);
	sigprocmask(SIG_SETMASK, &none, NULL);
	signal(SIGPIPE, SIG_DFL);
	signal(SIGCHLD, SIG_DFL);
	setsid();
	int in = slave >= 0 ? slave : open("/dev/null", O_RDWR | O_CLOEXEC);
	if (slave >= 0)
		ioctl(slave, TIOCSCTTY, 0);
	for (int fd = 0; in >= 0 && fd < 3; fd++)
		dup2(in, fd);
	/* Nothing of the daemon's (its listening socket) reaches the command. */
	syscall(SYS_close_range, 3, ~0U, CLOSE_RANGE_CLOEXEC);
	set_groups(spec);
	if (setresgid(spec->gid, spec->gid, spec->gid) || setresuid(spec->uid, spec->uid, spec->uid))
		fail_exit(status, "setuid", 127);
	if (chdir(spec->cwd) && chdir("/"))
		errno = 0;
	exec_command(spec->argv, spec->envp);
	report_error(status, spec->argv[0]);
	_exit(127);
}

static void run_entering_child(const struct sys_env *env, const struct exec_spec *spec, int slave, int status, int report)
{
	int ns[NS_COUNT];
	if (!env->is_rootless && join_cgroup(env, spec->cgroup_dir, (int)syscall(SYS_getpid)))
		errno = 0; /* stats miss it, nothing else */
	if (open_namespaces(spec->init_pid, ns))
		fail_exit(status, "namespaces", 1);
	for (size_t i = 0; i < NS_COUNT; i++)
		if (setns(ns[i], 0))
			fail_exit(status, NAMESPACES[i], 1);
	pid_t pid = fork();
	if (pid == 0)
		run_command(spec, slave, status);
	write_all(report, &pid, sizeof pid);
	_exit(0);
}

static int linux_exec_in(const struct sys_env *env, const struct exec_spec *spec, int *pty, int *pid, char *error, size_t cap)
{
	int status[2], report[2], slave = -1;
	*pty = spec->use_pty ? open_pty(spec, &slave) : -1;
	if ((spec->use_pty && *pty < 0) || pipe2(status, O_CLOEXEC) || pipe2(report, O_CLOEXEC)) {
		snprintf(error, cap, "pty: %s", strerror(errno));
		return -1;
	}
	pid_t child = fork();
	if (child == 0)
		run_entering_child(env, spec, slave, status[1], report[1]);
	close(status[1]), close(report[1]);
	if (slave >= 0)
		close(slave);
	pid_t host_pid = -1;
	int failed = child < 0 || read_all(report[0], &host_pid, sizeof host_pid);
	if (child > 0)
		waitpid(child, NULL, 0);
	ssize_t n = read(status[0], error, cap - 1);
	close(status[0]), close(report[0]);
	error[n > 0 ? n : 0] = 0;
	failed = failed || n > 0;
	*pid = failed ? -1 : find_ns_pid(env->proc, host_pid);
	if (failed && *pty >= 0)
		close(*pty), *pty = -1;
	return failed ? -1 : 0;
}

/* ---- the rest ------------------------------------------------------------------------- */

static int linux_send_signal(int pid, int sig) { return kill(pid, sig); }

static int linux_chown_at(void *ctx, int dirfd, const char *name, unsigned uid, unsigned gid)
{
	(void)ctx;
	return fchownat(dirfd, name, uid, gid, AT_SYMLINK_NOFOLLOW | (*name ? 0 : AT_EMPTY_PATH));
}

/* Host trials cannot give files away: owners stay the daemon's user. */
static int keep_owner(void *ctx, int dirfd, const char *name, unsigned uid, unsigned gid)
{
	(void)ctx, (void)dirfd, (void)name, (void)uid, (void)gid;
	return 0;
}

static int linux_set_window_size(int fd, int rows, int cols)
{
	struct winsize ws = { (unsigned short)rows, (unsigned short)cols, 0, 0 };
	return ioctl(fd, TIOCSWINSZ, &ws);
}

static void linux_sleep_ms(int ms)
{
	struct timespec ts = { ms / 1000, (long)(ms % 1000) * 1000000L };
	nanosleep(&ts, NULL);
}

const struct sys_env *linux_sys_env(void)
{
	static const struct sys_env env = { "/data/linux", "/data/media", "/data/misc/pc-wayland", "/sys/fs/cgroup",
					    "/proc", "/dev", "/data/system/packages.list", 0 };
	return &env;
}

const struct sys_ops *linux_sys_ops(void)
{
	static const struct sys_ops ops = { linux_start_init, linux_exec_in, linux_send_signal, linux_chown_at,
					    linux_set_window_size, linux_sleep_ms };
	return &ops;
}

const struct sys_ops *linux_rootless_sys_ops(void)
{
	static const struct sys_ops ops = { linux_start_init, linux_exec_in, linux_send_signal, keep_owner,
					    linux_set_window_size, linux_sleep_ms };
	return &ops;
}
