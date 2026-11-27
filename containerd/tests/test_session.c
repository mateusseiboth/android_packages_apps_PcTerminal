/* The person's session inside a running container (session.h), on the fake machine. */
#define _GNU_SOURCE
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include "check.h"
#include "fakesys.h"
#include "session.h"

#define ID_BASE 2001000000u
#define INIT_PID 5001

static char rootfs[128], cgroup_dir[128], bundle[128];
static char *const ENVP[] = { "HOME=/home/mateus", "XDG_RUNTIME_DIR=/run/user/1000", NULL };

/* A fresh container: an empty rootfs with these programs (755) in usr/bin, its init alone in the group. */
static void make_container(const char *const *programs, int count)
{
	char cmd[1024], procs[256];
	snprintf(cmd, sizeof cmd, "rm -rf %s %s && mkdir -p %s/usr/bin %s/etc %s", rootfs, cgroup_dir, rootfs, rootfs, cgroup_dir);
	CHECK(!system(cmd));
	for (int i = 0; i < count; i++) {
		snprintf(cmd, sizeof cmd, "printf '#!/bin/sh\\n' > %s/usr/bin/%s && chmod 755 %s/usr/bin/%s", rootfs, programs[i], rootfs,
			 programs[i]);
		CHECK(!system(cmd));
	}
	snprintf(procs, sizeof procs, "%s/cgroup.procs", cgroup_dir);
	fake_write(procs, "");
	fake_add_process(cgroup_dir, INIT_PID, "pc-init");
	fake.execs = 0;
}

/* The X11 bundle on the host (/system_ext/pc-x11): these programs in its bin, or no bundle at all. */
static void make_bundle(const char *const *programs, int count)
{
	char cmd[1024];
	snprintf(cmd, sizeof cmd, "rm -rf %s && mkdir -p %s/bin", bundle, bundle);
	CHECK(!system(cmd));
	for (int i = 0; i < count; i++) {
		snprintf(cmd, sizeof cmd, "printf '#!/bin/sh\\n' > %s/bin/%s && chmod 755 %s/bin/%s", bundle, programs[i], bundle,
			 programs[i]);
		CHECK(!system(cmd));
	}
}

static const char *const BUNDLE_PROGRAMS[] = { "dbus-daemon", "xwayland-satellite", "Xwayland" };

static int start(void)
{
	int root = open(rootfs, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
	struct session_target t = { &fake_env, &fake_ops, root, cgroup_dir, INIT_PID, ID_BASE, ENVP, "/home/mateus" };
	int started = start_session(&t);
	close(root);
	return started;
}

static const char *read_rootfs_file(const char *rel)
{
	static char text[4096];
	char path[512];
	snprintf(path, sizeof path, "%s/%s", rootfs, rel);
	FILE *f = fopen(path, "r");
	size_t n = f ? fread(text, 1, sizeof text - 1, f) : 0;
	if (f)
		fclose(f);
	text[n] = 0;
	return text;
}

static unsigned read_mode(const char *rel)
{
	char path[512];
	struct stat st;
	snprintf(path, sizeof path, "%s/%s", rootfs, rel);
	return lstat(path, &st) ? 0 : st.st_mode & 07777;
}

static const char *const EVERYTHING[] = { "dbus-daemon", "pipewire", "wireplumber", "pipewire-pulse", "xwayland-satellite", "Xwayland" };
#define COUNT(a) ((int)(sizeof a / sizeof *a))

static void starts_the_bus_the_sound_and_x11_as_the_user(void)
{
	make_container(EVERYTHING, COUNT(EVERYTHING));
	CHECK(start() == 3);
	CHECK(fake.execs == 3);
	CHECK_STR(fake.exec_log[0], "/usr/bin/dbus-daemon --session --address=unix:path=/run/user/1000/bus --nofork");
	CHECK_STR(fake.exec_log[1], "/usr/bin/pipewire");
	CHECK_STR(fake.exec_log[2], "/usr/bin/xwayland-satellite :0");
	CHECK(fake.last_uid == 1000 && fake.last_gid == 1000);
	CHECK(fake.last_init_pid == INIT_PID);
	CHECK_STR(fake.last_cwd, "/home/mateus");
	CHECK_STR(fake.last_env, "HOME=/home/mateus;XDG_RUNTIME_DIR=/run/user/1000;");
	CHECK(fake.last_use_pty == 0);
}

/* PipeWire's default sink is the compositor's pipe; PipeWire itself starts WirePlumber and pipewire-pulse
 * once its socket is up (no systemd to order them). */
static void writes_the_android_sink_for_pipewire(void)
{
	const char *conf = read_rootfs_file("etc/pipewire/pipewire.conf.d/pc-android.conf");
	CHECK(strstr(conf, "name = libpipewire-module-pipe-tunnel\n") != NULL);
	CHECK(strstr(conf, "tunnel.mode = sink\n") != NULL);
	CHECK(strstr(conf, "pipe.filename = \"/run/pc-wayland/audio-out\"\n") != NULL);
	CHECK(strstr(conf, "audio.format = S16LE\n") != NULL);
	CHECK(strstr(conf, "audio.rate = 48000\n") != NULL);
	CHECK(strstr(conf, "audio.channels = 2\n") != NULL);
	CHECK(strstr(conf, "audio.position = [ FL FR ]\n") != NULL);
	CHECK(strstr(conf, "node.name = android\n") != NULL);
	CHECK(strstr(conf, "flags = [ nofail ]\n") != NULL); /* no pipe yet: PipeWire still starts */
	CHECK(strstr(conf, "{ path = \"/usr/bin/wireplumber\" }\n") != NULL);
	CHECK(strstr(conf, "{ path = \"/usr/bin/pipewire-pulse\" }\n") != NULL);
	CHECK(read_mode("etc/pipewire") == 0755);
	CHECK(read_mode("etc/pipewire/pipewire.conf.d") == 0755);
	CHECK(read_mode("etc/pipewire/pipewire.conf.d/pc-android.conf") == 0644);
	CHECK_STR(fake.last_chown_name, ""); /* the file itself, by its fd */
	CHECK(fake.last_chown_uid == ID_BASE && fake.last_chown_gid == ID_BASE);
}

/* Every start and exec asks again: only what is not running starts. */
static void starts_only_what_is_not_running(void)
{
	make_container(EVERYTHING, COUNT(EVERYTHING));
	fake_add_process(cgroup_dir, 7001, "/usr/bin/dbus-daemon");
	fake_add_process(cgroup_dir, 7002, "pipewire");
	CHECK(start() == 1);
	CHECK_STR(fake.exec_log[0], "/usr/bin/xwayland-satellite :0");
	fake_add_process(cgroup_dir, 7003, "/usr/bin/xwayland-satellite");
	CHECK(start() == 0 && fake.execs == 1);
}

/* Fedora ships neither Xwayland nor dbus-daemon: the bundle mounted at /run/pc-x11 gives both. */
static void uses_the_bundle_for_what_the_distro_lacks(void)
{
	static const char *const FEDORA[] = { "dbus-broker-launch", "systemd-socket-activate", "pipewire" };
	make_bundle(BUNDLE_PROGRAMS, COUNT(BUNDLE_PROGRAMS));
	make_container(FEDORA, COUNT(FEDORA));
	CHECK(start() == 3);
	/* --config-file alone: with --session dbus-daemon also asks for the distro's session.conf and refuses both. */
	CHECK_STR(fake.exec_log[0], "/run/pc-x11/bin/dbus-daemon --config-file=/run/pc-x11/share/dbus-1/session.conf "
				    "--address=unix:path=/run/user/1000/bus --nofork");
	CHECK_STR(fake.exec_log[1], "/usr/bin/pipewire");
	CHECK_STR(fake.exec_log[2], "/run/pc-x11/bin/xwayland-satellite :0");
	fake_add_process(cgroup_dir, 7001, "/run/pc-x11/bin/dbus-daemon");
	fake_add_process(cgroup_dir, 7002, "/run/pc-x11/bin/xwayland-satellite");
	fake_add_process(cgroup_dir, 7003, "/usr/bin/pipewire");
	CHECK(start() == 0); /* seen running by their names */
	make_bundle(NULL, 0);
}

/* A distro's own Xwayland keeps the GPU, the bundle's does not: the distro's programs come first. */
/* The distro's bus, but the bundle's X11: its Xwayland has glamor, DRI3 and GLX on every distro. */
static void prefers_the_distros_bus_and_the_bundles_x11(void)
{
	make_bundle(BUNDLE_PROGRAMS, COUNT(BUNDLE_PROGRAMS));
	make_container(EVERYTHING, COUNT(EVERYTHING));
	CHECK(start() == 3);
	CHECK_STR(fake.exec_log[0], "/usr/bin/dbus-daemon --session --address=unix:path=/run/user/1000/bus --nofork");
	CHECK_STR(fake.exec_log[2], "/run/pc-x11/bin/xwayland-satellite :0");
	make_bundle(NULL, 0);
}

/* dbus-broker needs systemd's user instance, which no container runs: started anyway it leaves a socket
 * nobody answers, worse for programs than no bus at all. */
static void never_starts_dbus_broker(void)
{
	static const char *const BROKER[] = { "dbus-broker-launch", "systemd-socket-activate" };
	make_container(BROKER, COUNT(BROKER));
	CHECK(start() == 0 && fake.execs == 0);
}

static void skips_pieces_whose_programs_are_missing(void)
{
	static const char *const SATELLITE_ONLY[] = { "xwayland-satellite" };
	make_container(SATELLITE_ONLY, 1); /* no Xwayland to run */
	CHECK(start() == 0 && fake.execs == 0);
	make_container(NULL, 0);
	CHECK(start() == 0 && fake.execs == 0);
	CHECK(read_mode("etc/pipewire") == 0); /* nothing written without PipeWire */
	char cmd[512];
	make_container(EVERYTHING, 1);
	snprintf(cmd, sizeof cmd, "chmod 644 %s/usr/bin/dbus-daemon", rootfs);
	CHECK(!system(cmd));
	CHECK(start() == 0 && fake.execs == 0); /* not executable */
}

/* PipeWire alone: no session manager or pulse server to start from it. */
static void starts_pipewire_without_its_helpers(void)
{
	make_container(EVERYTHING + 1, 1);
	CHECK(start() == 1);
	CHECK_STR(fake.exec_log[0], "/usr/bin/pipewire");
	const char *conf = read_rootfs_file("etc/pipewire/pipewire.conf.d/pc-android.conf");
	CHECK(strstr(conf, "libpipewire-module-pipe-tunnel") != NULL);
	CHECK(strstr(conf, "context.exec") == NULL);
}

/* One missing piece or a failing exec never stops the rest. */
static void keeps_going_when_a_piece_fails(void)
{
	make_container(EVERYTHING, COUNT(EVERYTHING));
	fake.fail_exec = 1;
	CHECK(start() == 0);
	CHECK(fake.execs == 3);
	fake.fail_exec = 0;
}

/* The init's umask (077) must not hide the fragment from the user. */
static void writes_readable_files_under_any_umask(void)
{
	make_container(EVERYTHING + 1, 1);
	mode_t old = umask(077);
	CHECK(start() == 1);
	umask(old);
	CHECK(read_mode("etc/pipewire") == 0755);
	CHECK(read_mode("etc/pipewire/pipewire.conf.d") == 0755);
	CHECK(read_mode("etc/pipewire/pipewire.conf.d/pc-android.conf") == 0644);
}

int main(void)
{
	fake_setup();
	snprintf(rootfs, sizeof rootfs, "%s/rootfs", fake_root);
	snprintf(bundle, sizeof bundle, "%s/pc-x11", fake_root);
	fake_env.x11_bundle = bundle;
	snprintf(cgroup_dir, sizeof cgroup_dir, "%s/cgroup/pc-containers/u10-debian", fake_root);
	RUN(starts_the_bus_the_sound_and_x11_as_the_user);
	RUN(writes_the_android_sink_for_pipewire);
	RUN(starts_only_what_is_not_running);
	RUN(uses_the_bundle_for_what_the_distro_lacks);
	RUN(prefers_the_distros_bus_and_the_bundles_x11);
	RUN(never_starts_dbus_broker);
	RUN(skips_pieces_whose_programs_are_missing);
	RUN(starts_pipewire_without_its_helpers);
	RUN(keeps_going_when_a_piece_fails);
	RUN(writes_readable_files_under_any_umask);
	fake_cleanup();
	return CHECK_DONE();
}
