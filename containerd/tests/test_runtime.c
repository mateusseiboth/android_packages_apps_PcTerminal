/* Pieces of starting and entering a container that need no machine. */
#include "check.h"
#include "runtime.h"

static void finds_who_owns_the_storage(void)
{
	const char *list = "com.android.shell 2000 0 /data/user_de/0/com.android.shell platform:privapp 3003\n"
			   "com.android.providers.media.module 10161 0 /data/user/0/x platform:privapp 1023,1015\n";
	CHECK(find_storage_uid(list, 10) == 1010161u);
	CHECK(find_storage_uid(list, 0) == 10161u);
	CHECK(find_storage_uid("com.google.android.providers.media.module 10200 0 /x default 1023\n", 11) == 1110200u);
	CHECK(find_storage_uid("other 10000 0 /x default none\n", 10) == 0);
}

static void builds_the_environment_with_overrides(void)
{
	struct exec_env e;
	struct env_input in = { "mateus", 1000, "/home/mateus", "/bin/bash", 1 };
	const char *keys[] = { "LANG", "GDK_BACKEND" }, *values[] = { "pt_BR.UTF-8", "wayland" };
	CHECK(!build_exec_env(&e, &in, keys, values, 2));
	CHECK(e.count == 16);
	/* The X11 bundle last: a distro's own programs always come first. */
	CHECK_STR(e.vars[0], "PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin:/run/pc-x11/bin");
	CHECK_STR(e.vars[1], "HOME=/home/mateus");
	CHECK_STR(e.vars[5], "LANG=pt_BR.UTF-8");
	CHECK_STR(e.vars[6], "XDG_RUNTIME_DIR=/run/user/1000");
	CHECK_STR(e.vars[14], "TERM=xterm-256color");
	CHECK_STR(e.vars[15], "GDK_BACKEND=wayland");
	CHECK(e.vars[16] == NULL);
	const char *bad[] = { "A=B" }, *v[] = { "x" };
	CHECK(build_exec_env(&e, &in, bad, v, 1)); /* '=' in a name */
	in.use_pty = 0;
	CHECK(!build_exec_env(&e, &in, NULL, NULL, 0) && e.count == 14);
}

/* Qt and GTK pick Wayland, X11 programs find xwayland-satellite, and the session's bus and sound. */
static void points_programs_at_the_session(void)
{
	struct exec_env e;
	struct env_input in = { "mateus", 1000, "/home/mateus", "/bin/bash", 0 };
	const char *keys[] = { "DISPLAY", "QT_QPA_PLATFORM" }, *values[] = { ":1", "xcb" };
	CHECK(!build_exec_env(&e, &in, NULL, NULL, 0));
	CHECK_STR(e.vars[7], "WAYLAND_DISPLAY=/run/pc-wayland/wayland-0");
	CHECK_STR(e.vars[8], "XDG_SESSION_TYPE=wayland");
	CHECK_STR(e.vars[9], "QT_QPA_PLATFORM=wayland;xcb");
	CHECK_STR(e.vars[10], "DISPLAY=:0");
	CHECK_STR(e.vars[11], "DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/1000/bus");
	CHECK_STR(e.vars[12], "PULSE_SERVER=unix:/run/user/1000/pulse/native");
	CHECK_STR(e.vars[13], "container=pc-containerd");
	in.uid = 0;
	CHECK(!build_exec_env(&e, &in, keys, values, 2));
	CHECK_STR(e.vars[9], "QT_QPA_PLATFORM=xcb"); /* the request's values win */
	CHECK_STR(e.vars[10], "DISPLAY=:1");
	CHECK_STR(e.vars[11], "DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/0/bus");
	CHECK(e.count == 14);
}

int main(void)
{
	RUN(finds_who_owns_the_storage);
	RUN(builds_the_environment_with_overrides);
	RUN(points_programs_at_the_session);
	return CHECK_DONE();
}
