/* Requests in, replies out (PROTOCOL.md), on the fake machine. */
#define _GNU_SOURCE
#include <fcntl.h>
#include <signal.h>
#include <unistd.h>

#include "check.h"
#include "fakesys.h"
#include "json.h"
#include "server.h"
#include "tarmaker.h"

#define USER10 1001000u /* the system app of user 10 */
#define USER11 1101000u

static struct {
	char lines[64][8192];
	int fds[64];
	int count;
} out;

static void collect(void *ctx, const char *line, int fd)
{
	(void)ctx;
	if (out.count < 64) {
		snprintf(out.lines[out.count], sizeof out.lines[0], "%s", line);
		out.fds[out.count++] = fd;
		return;
	}
	if (fd >= 0)
		close(fd);
}

static struct server srv;

static const char *ask_with_fd(unsigned uid, const char *line, int fd)
{
	for (int i = 0; i < out.count; i++)
		if (out.fds[i] >= 0)
			close(out.fds[i]);
	out.count = 0;
	handle_request(&srv, uid, line, fd, collect, NULL);
	return out.count ? out.lines[out.count - 1] : "";
}

static const char *ask(unsigned uid, const char *line) { return ask_with_fd(uid, line, -1); }

static int open_tarball(void)
{
	struct tarball t = { 0 };
	char path[256];
	tar_dir(&t, "./");
	tar_file(&t, "./etc/os-release", "ID=debian\n");
	tar_file(&t, "./etc/passwd", "root:x:0:0:root:/root:/bin/bash\nmateus:x:1000:1000::/home/mateus:/bin/zsh\n");
	tar_file(&t, "./usr/share/applications/org.x.Editor.desktop",
		 "[Desktop Entry]\nType=Application\nName=Editor\nName[pt]=Editor PT\nExec=editor %F\nIcon=editor\n");
	tar_file(&t, "./usr/share/icons/hicolor/256x256/apps/editor.png", "PNGDATA");
	tar_end(&t);
	snprintf(path, sizeof path, "%s/stream.tar", fake_root);
	FILE *f = fopen(path, "w");
	fwrite(t.data, 1, t.len, f);
	fclose(f);
	free(t.data);
	return open(path, O_RDONLY | O_CLOEXEC);
}

static void creates_a_container_from_a_tar_fd(void)
{
	CHECK_STR(ask(USER10, "{\"id\":1,\"cmd\":\"list\"}"), "{\"id\":1,\"ok\":true,\"containers\":[]}\n");
	const char *reply = ask_with_fd(USER10, "{\"id\":2,\"cmd\":\"create\",\"name\":\"debian\",\"distro\":\"debian\","
						"\"release\":\"trixie\",\"user\":\"mateus\"}",
					open_tarball());
	CHECK(strstr(reply, "{\"id\":2,\"ok\":true,\"container\":{\"name\":\"debian\",\"distro\":\"debian\",\"release\":\"trixie\",\"created\":"));
	CHECK(strstr(reply, "\"user\":\"mateus\",\"state\":\"stopped\"}}\n"));
	CHECK(strstr(out.lines[0], "{\"id\":2,\"event\":\"progress\",\"bytes\":")); /* at least the first entry */
	reply = ask(USER10, "{\"id\":3,\"cmd\":\"list\"}");
	CHECK(strstr(reply, "\"containers\":[{\"name\":\"debian\""));
	CHECK(access(fake_path("linux/10/debian/rootfs/etc/os-release"), F_OK) == 0);
	CHECK(access(fake_path("linux/10/debian/config.json"), F_OK) == 0);
}

static void create_refuses_bad_requests(void)
{
	CHECK(strstr(ask_with_fd(USER10, "{\"id\":4,\"cmd\":\"create\",\"name\":\"debian\",\"distro\":\"d\",\"release\":\"r\"}", open_tarball()),
		     "\"error\":\"exists\""));
	CHECK(strstr(ask_with_fd(USER10, "{\"id\":5,\"cmd\":\"create\",\"name\":\"Bad Name\",\"distro\":\"d\",\"release\":\"r\"}", open_tarball()),
		     "\"error\":\"bad_name\""));
	CHECK(strstr(ask_with_fd(USER10, "{\"id\":6,\"cmd\":\"create\",\"name\":\"x\",\"distro\":\"d\",\"release\":\"r\",\"user\":\"root\"}",
				 open_tarball()),
		     "\"error\":\"bad_user\""));
	CHECK(strstr(ask(USER10, "{\"id\":7,\"cmd\":\"create\",\"name\":\"x\",\"distro\":\"d\",\"release\":\"r\"}"), "\"error\":\"no_fd\""));
	int devnull = open("/dev/null", O_RDONLY | O_CLOEXEC);
	CHECK(strstr(ask_with_fd(USER10, "{\"id\":8,\"cmd\":\"create\",\"name\":\"x\",\"distro\":\"d\",\"release\":\"r\"}", devnull),
		     "\"error\":\"bad_archive\""));
	CHECK(access(fake_path("linux/10/x"), F_OK) != 0); /* nothing left behind */
}

static void each_android_user_sees_only_their_containers(void)
{
	CHECK_STR(ask(USER11, "{\"id\":9,\"cmd\":\"list\",\"user_id\":10}"), "{\"id\":9,\"ok\":true,\"containers\":[]}\n");
	CHECK(strstr(ask(USER11, "{\"id\":10,\"cmd\":\"start\",\"name\":\"debian\"}"), "\"error\":\"not_found\""));
}

/* /data/misc/pc-wayland/<userId>: 0711, owned by that user's system uid, where the compositor puts its socket. */
static int is_wayland_dir_ready(unsigned user)
{
	char rel[32];
	struct stat st;
	snprintf(rel, sizeof rel, "wayland/%u", user);
	return !stat(fake_path(rel), &st) && S_ISDIR(st.st_mode) && (st.st_mode & 07777) == 0711 &&
	       !strcmp(fake.last_chown_name, rel + 8) && fake.last_chown_uid == user * 100000 + 1000 &&
	       fake.last_chown_gid == user * 100000 + 1000;
}

static void the_wayland_folder_is_put_right_at_every_start(void)
{
	ask(USER10, "{\"id\":19,\"cmd\":\"stop\",\"name\":\"debian\"}");
	CHECK(!chmod(fake_path("wayland/10"), 0750));
	memset(fake.last_chown_name, 0, sizeof fake.last_chown_name);
	CHECK(strstr(ask(USER10, "{\"id\":19,\"cmd\":\"start\",\"name\":\"debian\"}"), "\"ok\":true"));
	CHECK(is_wayland_dir_ready(10));
	ask(USER10, "{\"id\":19,\"cmd\":\"stop\",\"name\":\"debian\"}");
}

static void starts_once_and_stops(void)
{
	CHECK_STR(ask(USER10, "{\"id\":11,\"cmd\":\"start\",\"name\":\"debian\",\"dns\":[\"192.168.0.1\"]}"),
		  "{\"id\":11,\"ok\":true,\"state\":\"running\"}\n");
	CHECK(fake.starts == 1);
	CHECK_STR(fake.last_name, "debian");
	CHECK_STR(fake.last_user, "mateus");
	CHECK_STR(fake.last_resolv, "nameserver 192.168.0.1\n");
	CHECK(fake.last_id_base == 2148500000u);
	CHECK(fake.last_storage_uid == 1010161u); /* MediaProvider of user 10 */
	CHECK(fake.last_plan_count > 10);
	CHECK(access(fake_path("wayland/10"), F_OK) == 0);
	CHECK(is_wayland_dir_ready(10));
	CHECK(ask(USER10, "{\"id\":12,\"cmd\":\"start\",\"name\":\"debian\"}") && fake.starts == 1);
	CHECK(strstr(ask(USER10, "{\"id\":13,\"cmd\":\"list\"}"), "\"state\":\"running\""));
	CHECK(strstr(ask(USER10, "{\"id\":14,\"cmd\":\"start\",\"name\":\"debian\",\"dns\":[\"x\"]}"), "\"ok\":true")); /* running: dns unused */
	CHECK_STR(ask(USER10, "{\"id\":15,\"cmd\":\"stop\",\"name\":\"debian\"}"), "{\"id\":15,\"ok\":true,\"state\":\"stopped\"}\n");
	CHECK(fake.last_signal == SIGTERM);
	CHECK(strstr(ask(USER10, "{\"id\":16,\"cmd\":\"list\"}"), "\"state\":\"stopped\""));
	CHECK(access(fake_path("cgroup/pc-containers/u10-debian"), F_OK) != 0);
	CHECK(strstr(ask(USER10, "{\"id\":17,\"cmd\":\"stop\",\"name\":\"debian\"}"), "\"ok\":true"));
	CHECK(strstr(ask(USER10, "{\"id\":18,\"cmd\":\"start\",\"name\":\"debian\",\"dns\":[\"1.1.1.1\\nsearch x\"]}"),
		     "\"error\":\"bad_request\""));
}

static void exec_starts_the_container_and_hands_back_the_pty(void)
{
	const char *reply = ask(USER10, "{\"id\":20,\"cmd\":\"exec\",\"name\":\"debian\",\"pty\":true,\"rows\":40,\"cols\":100}");
	CHECK_STR(reply, "{\"id\":20,\"ok\":true,\"pid\":42,\"fd\":true}\n");
	CHECK(out.fds[out.count - 1] >= 0);
	CHECK(fake.starts == 3); /* started, then restarted by the Wayland test */
	CHECK_STR(fake.last_argv0, "/bin/zsh"); /* the user's shell from the container's passwd */
	CHECK_STR(fake.last_cwd, "/home/mateus");
	CHECK(fake.last_uid == 1000);
	CHECK(strstr(fake.last_env, "TERM=xterm-256color;"));
	CHECK(strstr(fake.last_env, "HOME=/home/mateus;"));
	CHECK(strstr(fake.last_env, "WAYLAND_DISPLAY=/run/pc-wayland/wayland-0;"));
	CHECK(strstr(fake.last_env, "XDG_RUNTIME_DIR=/run/user/1000;"));

	reply = ask(USER10, "{\"id\":21,\"cmd\":\"exec\",\"name\":\"debian\",\"argv\":[\"gedit\"],\"env\":{\"GDK_BACKEND\":\"wayland\","
			    "\"HOME\":\"/tmp\"},\"user\":\"root\"}");
	CHECK_STR(reply, "{\"id\":21,\"ok\":true,\"pid\":42}\n");
	CHECK(out.fds[out.count - 1] < 0);
	CHECK_STR(fake.last_argv0, "gedit");
	CHECK_STR(fake.last_cwd, "/root");
	CHECK(fake.last_uid == 0);
	CHECK(strstr(fake.last_env, "GDK_BACKEND=wayland;"));
	CHECK(strstr(fake.last_env, "HOME=/tmp;") && !strstr(fake.last_env, "HOME=/root;"));
	CHECK(!strstr(fake.last_env, "TERM="));

	CHECK(strstr(ask(USER10, "{\"id\":22,\"cmd\":\"exec\",\"name\":\"debian\",\"user\":\"someone\"}"), "\"error\":\"bad_user\""));
	CHECK(strstr(ask(USER10, "{\"id\":23,\"cmd\":\"exec\",\"name\":\"debian\",\"argv\":[]}"), "\"error\":\"bad_request\""));
	fake.fail_exec = 1;
	reply = ask(USER10, "{\"id\":24,\"cmd\":\"exec\",\"name\":\"debian\",\"argv\":[\"nope\"]}");
	CHECK_STR(reply, "{\"id\":24,\"ok\":false,\"error\":\"exec_failed\",\"message\":\"No such file or directory\"}\n");
	fake.fail_exec = 0;
}

static void resizes_the_pty_it_is_given(void)
{
	CHECK_STR(ask_with_fd(USER10, "{\"id\":25,\"cmd\":\"resize\",\"rows\":50,\"cols\":132}", dup(0)), "{\"id\":25,\"ok\":true}\n");
	CHECK(fake.rows == 50 && fake.cols == 132);
	CHECK(strstr(ask(USER10, "{\"id\":26,\"cmd\":\"resize\",\"rows\":50,\"cols\":132}"), "\"error\":\"no_fd\""));
	CHECK(strstr(ask_with_fd(USER10, "{\"id\":27,\"cmd\":\"resize\",\"rows\":0,\"cols\":132}", dup(0)), "\"error\":\"bad_request\""));
}

static void lists_apps_and_opens_icons(void)
{
	const char *reply = ask(USER10, "{\"id\":30,\"cmd\":\"apps\",\"name\":\"debian\",\"locale\":\"pt_BR\"}");
	CHECK_STR(reply, "{\"id\":30,\"ok\":true,\"apps\":[{\"id\":\"org.x.Editor\",\"name\":\"Editor PT\",\"exec\":\"editor %F\","
			 "\"argv\":[\"editor\"],\"icon\":\"editor\",\"icon_path\":\"/usr/share/icons/hicolor/256x256/apps/editor.png\","
			 "\"icon_format\":\"png\",\"terminal\":false,\"categories\":[]}]}\n");
	reply = ask(USER10, "{\"id\":31,\"cmd\":\"icon\",\"name\":\"debian\",\"app\":\"org.x.Editor\"}");
	CHECK_STR(reply, "{\"id\":31,\"ok\":true,\"fd\":true,\"format\":\"png\",\"size\":7}\n");
	char data[16] = { 0 };
	CHECK(out.fds[0] >= 0 && read(out.fds[0], data, sizeof data - 1) == 7);
	CHECK_STR(data, "PNGDATA");
	reply = ask(USER10, "{\"id\":32,\"cmd\":\"icon\",\"name\":\"debian\",\"path\":\"/usr/share/icons/hicolor/256x256/apps/editor.png\"}");
	CHECK(strstr(reply, "\"ok\":true,\"fd\":true"));
	CHECK(strstr(ask(USER10, "{\"id\":33,\"cmd\":\"icon\",\"name\":\"debian\",\"path\":\"/etc/passwd\"}"), "\"error\":\"no_icon\""));
	CHECK(strstr(ask(USER10, "{\"id\":34,\"cmd\":\"icon\",\"name\":\"debian\",\"app\":\"missing\"}"), "\"error\":\"no_icon\""));
	CHECK(strstr(ask(USER10, "{\"id\":35,\"cmd\":\"icon\",\"name\":\"debian\"}"), "\"error\":\"bad_request\""));
}

static void reports_stats(void)
{
	fake_write(fake_path("cgroup/pc-containers/u10-debian/memory.current"), "1000\n");
	fake_write(fake_path("cgroup/pc-containers/u10-debian/cpu.stat"), "usage_usec 77\n");
	const char *reply = ask(USER10, "{\"id\":40,\"cmd\":\"stats\",\"name\":\"debian\"}");
	CHECK(strstr(reply, "{\"id\":40,\"ok\":true,\"state\":\"running\",\"memory_bytes\":1000,\"cpu_usec\":77,\"pids\":1,\"disk_bytes\":"));
	ask(USER10, "{\"id\":41,\"cmd\":\"stop\",\"name\":\"debian\"}");
	reply = ask(USER10, "{\"id\":42,\"cmd\":\"stats\",\"name\":\"debian\"}");
	CHECK(strstr(reply, "{\"id\":42,\"ok\":true,\"state\":\"stopped\",\"memory_bytes\":null,\"cpu_usec\":null,\"pids\":null,\"disk_bytes\":"));
}

static void renames_and_deletes(void)
{
	ask(USER10, "{\"id\":50,\"cmd\":\"start\",\"name\":\"debian\"}");
	CHECK(strstr(ask(USER10, "{\"id\":51,\"cmd\":\"rename\",\"name\":\"debian\",\"new_name\":\"work\"}"), "\"error\":\"running\""));
	ask(USER10, "{\"id\":52,\"cmd\":\"stop\",\"name\":\"debian\"}");
	CHECK(strstr(ask(USER10, "{\"id\":53,\"cmd\":\"rename\",\"name\":\"debian\",\"new_name\":\"Work\"}"), "\"error\":\"bad_name\""));
	CHECK(strstr(ask(USER10, "{\"id\":54,\"cmd\":\"rename\",\"name\":\"debian\",\"new_name\":\"work\"}"),
		     "{\"id\":54,\"ok\":true,\"container\":{\"name\":\"work\""));
	ask(USER10, "{\"id\":55,\"cmd\":\"start\",\"name\":\"work\"}");
	CHECK_STR(ask(USER10, "{\"id\":56,\"cmd\":\"delete\",\"name\":\"work\"}"), "{\"id\":56,\"ok\":true}\n");
	CHECK(access(fake_path("linux/10/work"), F_OK) != 0);
	CHECK(access(fake_path("cgroup/pc-containers/u10-work"), F_OK) != 0);
	CHECK(strstr(ask(USER10, "{\"id\":57,\"cmd\":\"delete\",\"name\":\"work\"}"), "\"error\":\"not_found\""));
}

static void answers_bad_lines(void)
{
	CHECK_STR(ask(USER10, "not json"), "{\"id\":0,\"ok\":false,\"error\":\"bad_request\",\"message\":\"not a JSON request\"}\n");
	CHECK(strstr(ask(USER10, "{\"cmd\":\"list\"}"), "{\"id\":0,\"ok\":false,\"error\":\"bad_request\""));
	CHECK(strstr(ask(USER10, "{\"id\":60,\"cmd\":\"format\"}"), "{\"id\":60,\"ok\":false,\"error\":\"bad_request\""));
	CHECK_STR(ask(USER10, "{\"id\":61,\"cmd\":\"version\"}"), "{\"id\":61,\"ok\":true,\"protocol\":1}\n");
	CHECK(strstr(ask_with_fd(USER10, "{\"id\":62,\"cmd\":\"list\"}", dup(0)), "\"ok\":true")); /* a stray fd is closed */
}

int main(void)
{
	fake_setup();
	server_init(&srv, &fake_env, &fake_ops);
	RUN(creates_a_container_from_a_tar_fd);
	RUN(create_refuses_bad_requests);
	RUN(each_android_user_sees_only_their_containers);
	RUN(starts_once_and_stops);
	RUN(the_wayland_folder_is_put_right_at_every_start);
	RUN(exec_starts_the_container_and_hands_back_the_pty);
	RUN(resizes_the_pty_it_is_given);
	RUN(lists_apps_and_opens_icons);
	RUN(reports_stats);
	RUN(renames_and_deletes);
	RUN(answers_bad_lines);
	fake_cleanup();
	return CHECK_DONE();
}
