#define _GNU_SOURCE
#include "session.h"

#include <fcntl.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

#include "cgroup.h"
#include "ids.h"
#include "inroot.h"

#define NEEDS_MAX 3
#define RUNS_AS_MAX 4
#define ARGS_MAX 12
#define PROGRAM_PATH_MAX 128
#define PROGRAMS_TEXT_MAX (1 << 16)
#define AUDIO_CONF_DIR "etc/pipewire/pipewire.conf.d"
#define AUDIO_CONF AUDIO_CONF_DIR "/pc-android.conf"
/* Where the X11 bundle is mounted in every container (mounts.c). */
#define BUNDLE_ROOT "/run/pc-x11"

/* Where a program's name is looked up in the rootfs, as the PATH of the exec environment would. */
static const char *const PROGRAM_DIRS[] = { "usr/local/bin", "usr/bin", "bin", "usr/sbin", "sbin" };

/* An executable file of that name, as an absolute path inside the container; -1 when there is none. */
static int find_program(int root, const char *name, char *out, size_t cap)
{
	char rel[PATH_MAX];
	struct stat st;
	for (size_t i = 0; i < sizeof PROGRAM_DIRS / sizeof *PROGRAM_DIRS; i++) {
		snprintf(rel, sizeof rel, "%s/%s", PROGRAM_DIRS[i], name);
		int fd = open_in_root(root, rel, O_PATH, 0);
		int is_program = fd >= 0 && !fstat(fd, &st) && S_ISREG(st.st_mode) && (st.st_mode & 0111);
		if (fd >= 0)
			close(fd);
		if (is_program) {
			snprintf(out, cap, "/%s", rel);
			return 0;
		}
	}
	return -1;
}

/* ---- commands ----------------------------------------------------------------------------- */

struct command {
	char *argv[ARGS_MAX + 1];
	int count;
	char text[1024];
	size_t used;
};

static void add_arg(struct command *c, const char *format, ...)
{
	va_list ap;
	char *arg = c->text + c->used;
	va_start(ap, format);
	int n = vsnprintf(arg, sizeof c->text - c->used, format, ap);
	va_end(ap);
	if (n < 0 || c->used + (size_t)n + 1 > sizeof c->text || c->count == ARGS_MAX)
		return;
	c->used += (size_t)n + 1;
	c->argv[c->count++] = arg;
	c->argv[c->count] = NULL;
}

typedef char program_paths[NEEDS_MAX][PROGRAM_PATH_MAX];

static void build_dbus_daemon(struct command *c, program_paths paths)
{
	add_arg(c, "%s", paths[0]);
	add_arg(c, "--session");
	add_arg(c, "--address=unix:path=/run/user/%u/bus", CONTAINER_USER_ID);
	add_arg(c, "--nofork");
}

/* The bundle's dbus-daemon reads its own session.conf: the distro may have none. --config-file alone, as
 * --session would also ask for the distro's file and dbus-daemon refuses both. */
static void build_bundle_dbus_daemon(struct command *c, program_paths paths)
{
	add_arg(c, "%s", paths[0]);
	add_arg(c, "--config-file=%s/share/dbus-1/session.conf", BUNDLE_ROOT);
	add_arg(c, "--address=unix:path=/run/user/%u/bus", CONTAINER_USER_ID);
	add_arg(c, "--nofork");
}

/* The bundle's system bus for every distro (no container runs systemd, which starts the distro's): Flatpak
 * installs nothing without one. */
static void build_system_bus(struct command *c, program_paths paths)
{
	add_arg(c, "%s", paths[0]);
	add_arg(c, "--config-file=%s/share/dbus-1/system.conf", BUNDLE_ROOT);
	add_arg(c, "--nopidfile");
	add_arg(c, "--nofork");
}

/* Flatpak's system repository, empty: without it every flatpak command of the user fails, even with the
 * user's own installation (FLATPAK_USER_DIR, runtime.c). Only root makes it; once, at the first start
 * after Flatpak is installed. */
static void build_flatpak_repo(struct command *c, program_paths paths)
{
	add_arg(c, "%s", paths[0]);
	add_arg(c, "remotes");
	add_arg(c, "--system");
}

static int has_flatpak_repo(const struct session_target *t)
{
	int fd = open_in_root(t->root_fd, "var/lib/flatpak/repo/config", O_PATH, 0);
	if (fd >= 0)
		close(fd);
	return fd >= 0;
}

static void build_satellite(struct command *c, program_paths paths)
{
	add_arg(c, "%s", paths[0]);
	add_arg(c, ":0");
}

static void build_pipewire(struct command *c, program_paths paths) { add_arg(c, "%s", paths[0]); }

/* ---- PipeWire's Android sink ---------------------------------------------------------------- */

/* Started by PipeWire itself once its socket listens: no systemd orders them here. */
static const char *const AUDIO_HELPERS[] = { "wireplumber", "pipewire-pulse" };

static const char AUDIO_SINK[] =
	"# pc-containerd writes this file before it starts PipeWire (README, \"Session\").\n"
	"# The default sink is the compositor's pipe: it plays what is written there through Android.\n"
	"context.modules = [\n"
	"    {   name = libpipewire-module-pipe-tunnel\n"
	"        args = {\n"
	"            tunnel.mode = sink\n"
	"            pipe.filename = \"/run/pc-wayland/audio-out\"\n"
	"            audio.format = S16LE\n"
	"            audio.rate = 48000\n"
	"            audio.channels = 2\n"
	"            audio.position = [ FL FR ]\n"
	"            node.name = android\n"
	"            stream.props = {\n"
	"                node.description = \"Android\"\n"
	"                priority.session = 3000\n"
	"            }\n"
	"        }\n"
	"        flags = [ nofail ]\n"
	"    }\n"
	"]\n";

static void build_audio_conf(int root, char *out, size_t cap)
{
	char path[PROGRAM_PATH_MAX];
	size_t used = (size_t)snprintf(out, cap, "%s", AUDIO_SINK);
	const char *open = "context.exec = [\n";
	for (size_t i = 0; i < sizeof AUDIO_HELPERS / sizeof *AUDIO_HELPERS && used < cap; i++) {
		if (find_program(root, AUDIO_HELPERS[i], path, sizeof path))
			continue;
		used += (size_t)snprintf(out + used, cap - used, "%s    { path = \"%s\" }\n", open, path);
		open = "";
	}
	if (!*open && used < cap)
		snprintf(out + used, cap - used, "]\n");
}

/* Folders made for the fragment: the container's root owns them and everyone reads them, whatever the
 * daemon's umask. */
static int give_dir_to_container(void *ctx, int dirfd, const char *name)
{
	const struct session_target *t = ctx;
	fchmodat(dirfd, name, 0755, 0);
	return t->ops->chown_at(NULL, dirfd, name, t->id_base, t->id_base);
}

static int write_audio_conf(const struct session_target *t)
{
	char text[4096];
	build_audio_conf(t->root_fd, text, sizeof text);
	if (make_dirs_in_root(t->root_fd, AUDIO_CONF_DIR, 0755, give_dir_to_container, (void *)t))
		return -1;
	int fd = open_in_root(t->root_fd, AUDIO_CONF, O_WRONLY | O_CREAT | O_TRUNC | O_NOFOLLOW, 0644);
	if (fd < 0)
		return -1;
	size_t len = strlen(text);
	int failed = write(fd, text, len) != (ssize_t)len || fchmod(fd, 0644) || t->ops->chown_at(NULL, fd, "", t->id_base, t->id_base);
	close(fd);
	return failed ? -1 : 0;
}

/* ---- the pieces ---------------------------------------------------------------------------- */

struct launcher {
	const char *needs[NEEDS_MAX]; /* programs that must all be there; NULL ends the list */
	void (*build)(struct command *c, program_paths paths);
	int from_bundle;              /* the programs come from the X11 bundle, not the rootfs */
};

struct piece {
	const char *name;
	const char *runs_as[RUNS_AS_MAX]; /* argv[0] base names that show the piece runs */
	struct launcher launchers[2];     /* the first whose programs are all there */
	int (*prepare)(const struct session_target *t);
	int is_root;                      /* runs as the container's root, not its user */
	int (*is_done)(const struct session_target *t); /* a one-time piece: nothing left to do */
};

/* The distro's programs first, then the bundle's; X11 the other way round: the bundle's Xwayland has glamor,
 * DRI3 and GLX with its own Mesa, the same on every distro, and a distro's is only the fallback. No dbus-broker: it needs
 * systemd's user instance, which no container runs, and left alone it keeps a socket nobody answers. */
static const struct piece PIECES[] = {
	{ "system-bus", { "pc-system-bus" }, { { { "pc-system-bus" }, build_system_bus, 1 } }, NULL, 1, NULL },
	{ "flatpak-repo", { "flatpak" }, { { { "flatpak" }, build_flatpak_repo, 0 } }, NULL, 1, has_flatpak_repo },
	{ "bus",
	  { "dbus-daemon" },
	  { { { "dbus-daemon" }, build_dbus_daemon, 0 }, { { "dbus-daemon" }, build_bundle_dbus_daemon, 1 } },
	  NULL, 0, NULL },
	{ "sound", { "pipewire" }, { { { "pipewire" }, build_pipewire, 0 } }, write_audio_conf, 0, NULL },
	{ "x11",
	  { "xwayland-satellite" },
	  { { { "xwayland-satellite", "Xwayland" }, build_satellite, 1 }, { { "xwayland-satellite", "Xwayland" }, build_satellite, 0 } },
	  NULL, 0, NULL },
};

static int has_line(const char *text, const char *line)
{
	size_t len = strlen(line);
	for (const char *p = text; *p; p += strcspn(p, "\n") + 1)
		if (!strncmp(p, line, len) && p[len] == '\n')
			return 1;
	return 0;
}

static int is_running(const struct piece *p, const char *programs)
{
	for (int i = 0; i < RUNS_AS_MAX && p->runs_as[i]; i++)
		if (has_line(programs, p->runs_as[i]))
			return 1;
	return 0;
}

/* A program of the bundle: checked on the host, where the daemon sees the bundle, and named by its path in
 * the container. */
static int find_bundle_program(const char *bundle, const char *name, char *out, size_t cap)
{
	char host[PATH_MAX];
	if (!bundle)
		return -1;
	snprintf(host, sizeof host, "%s/bin/%s", bundle, name);
	if (access(host, X_OK))
		return -1;
	snprintf(out, cap, "%s/bin/%s", BUNDLE_ROOT, name);
	return 0;
}

static int find_launcher_program(const struct session_target *t, const struct launcher *l, const char *name, char *out,
				 size_t cap)
{
	return l->from_bundle ? find_bundle_program(t->env->x11_bundle, name, out, cap) : find_program(t->root_fd, name, out, cap);
}

static int find_launcher_programs(const struct session_target *t, const struct launcher *l, program_paths paths)
{
	if (!l->build)
		return -1;
	for (int i = 0; i < NEEDS_MAX && l->needs[i]; i++)
		if (find_launcher_program(t, l, l->needs[i], paths[i], sizeof paths[i]))
			return -1;
	return 0;
}

static int run_command(const struct session_target *t, const struct piece *p, struct command *c)
{
	char error[256] = "";
	int pty = -1, pid = 0;
	unsigned id = p->is_root ? 0 : CONTAINER_USER_ID;
	struct exec_spec spec = { t->init_pid, t->cgroup_dir, c->argv, t->envp, p->is_root ? "/" : t->home, id, id,
				  0, 0, 0, t->id_base, t->env->is_rootless };
	if (t->ops->exec_in(t->env, &spec, &pty, &pid, error, sizeof error)) {
		fprintf(stderr, "pc-containerd: session %s: %s\n", p->name, error);
		return 0;
	}
	return 1;
}

static int start_piece(const struct session_target *t, const struct piece *p)
{
	program_paths paths;
	for (size_t i = 0; i < sizeof p->launchers / sizeof *p->launchers; i++) {
		struct command c = { .count = 0 };
		if (find_launcher_programs(t, &p->launchers[i], paths))
			continue;
		if (p->prepare && p->prepare(t)) {
			fprintf(stderr, "pc-containerd: session %s: could not write its files\n", p->name);
			return 0;
		}
		p->launchers[i].build(&c, paths);
		return run_command(t, p, &c);
	}
	return 0;
}

int start_session(const struct session_target *t)
{
	static char programs[PROGRAMS_TEXT_MAX];
	int started = 0;
	/* Requests are served by one process per connection: two execs at once must not both start a piece. */
	flock(t->root_fd, LOCK_EX);
	if (list_group_programs(t->cgroup_dir, t->env->proc, programs, sizeof programs) >= 0)
		for (size_t i = 0; i < sizeof PIECES / sizeof *PIECES; i++)
			started += !(PIECES[i].is_done && PIECES[i].is_done(t)) && !is_running(&PIECES[i], programs) &&
				   start_piece(t, &PIECES[i]);
	flock(t->root_fd, LOCK_UN);
	return started;
}
