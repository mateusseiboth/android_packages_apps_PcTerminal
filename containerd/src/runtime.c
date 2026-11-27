#define _GNU_SOURCE
#include "runtime.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ids.h"

/* MediaProvider's package, AOSP's or the Google build of the module. */
static const char *const MEDIA_PACKAGES[] = { "com.android.providers.media.module", "com.google.android.providers.media.module" };

static int is_media_package(const char *line, size_t len)
{
	for (size_t i = 0; i < sizeof MEDIA_PACKAGES / sizeof *MEDIA_PACKAGES; i++) {
		size_t n = strlen(MEDIA_PACKAGES[i]);
		if (len > n && !strncmp(line, MEDIA_PACKAGES[i], n) && line[n] == ' ')
			return 1;
	}
	return 0;
}

unsigned find_storage_uid(const char *packages_list, unsigned user)
{
	for (const char *p = packages_list; *p;) {
		size_t len = strcspn(p, "\n");
		if (is_media_package(p, len)) {
			unsigned app_id = (unsigned)strtoul(p + strcspn(p, " "), NULL, 10);
			return app_id ? user * AID_USER_OFFSET + app_id : 0;
		}
		p += len + (p[len] == '\n');
	}
	return 0;
}

struct env_builder {
	struct exec_env *e;
	size_t used;
};

static int find_var(const struct exec_env *e, const char *key)
{
	size_t len = strlen(key);
	for (int i = 0; i < e->count; i++)
		if (!strncmp(e->vars[i], key, len) && e->vars[i][len] == '=')
			return i;
	return -1;
}

static int set_var(struct env_builder *b, const char *key, const char *value)
{
	size_t need = strlen(key) + strlen(value) + 2;
	int at = find_var(b->e, key);
	if (!*key || strchr(key, '=') || b->used + need > sizeof b->e->buf || (at < 0 && b->e->count == EXEC_ENV_MAX))
		return -1;
	char *var = b->e->buf + b->used;
	snprintf(var, need, "%s=%s", key, value);
	b->used += need;
	if (at < 0)
		at = b->e->count++;
	b->e->vars[at] = var;
	b->e->vars[b->e->count] = NULL;
	return 0;
}

int build_exec_env(struct exec_env *e, const struct env_input *in, const char *const *keys, const char *const *values, int count)
{
	char runtime_dir[32], bus[64], pulse[64];
	struct env_builder b = { e, 0 };
	e->count = 0;
	e->vars[0] = NULL;
	snprintf(runtime_dir, sizeof runtime_dir, "/run/user/%u", in->uid);
	snprintf(bus, sizeof bus, "unix:path=%s/bus", runtime_dir);
	snprintf(pulse, sizeof pulse, "unix:%s/pulse/native", runtime_dir);
	const char *const DEFAULTS[][2] = {
		/* The X11 bundle last: a distro's own programs always come first. */
		{ "PATH", "/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin:/run/pc-x11/bin" },
		{ "HOME", in->home },
		{ "USER", in->user },
		{ "LOGNAME", in->user },
		{ "SHELL", in->shell },
		{ "LANG", "C.UTF-8" },
		{ "XDG_RUNTIME_DIR", runtime_dir },
		{ "WAYLAND_DISPLAY", "/run/pc-wayland/wayland-0" },
		/* Without it Qt 6 tries X11 first. */
		{ "XDG_SESSION_TYPE", "wayland" },
		{ "QT_QPA_PLATFORM", "wayland;xcb" },
		/* The session the daemon starts inside (README, "Session"). */
		{ "DISPLAY", ":0" },
		{ "DBUS_SESSION_BUS_ADDRESS", bus },
		{ "PULSE_SERVER", pulse },
		{ "container", "pc-containerd" },
		/* flatpak --user installs in the container, where the menu finds its apps (container_files.c). */
		{ "FLATPAK_USER_DIR", "/var/lib/flatpak-user" },
	};
	int failed = 0;
	for (size_t i = 0; i < sizeof DEFAULTS / sizeof *DEFAULTS; i++)
		failed |= set_var(&b, DEFAULTS[i][0], DEFAULTS[i][1]);
	if (in->use_pty)
		failed |= set_var(&b, "TERM", "xterm-256color");
	for (int i = 0; i < count; i++)
		failed |= set_var(&b, keys[i], values[i]);
	return failed ? -1 : 0;
}
