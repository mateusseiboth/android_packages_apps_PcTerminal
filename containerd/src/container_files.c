#define _GNU_SOURCE
#include "container_files.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

#include "accounts.h"
#include "ids.h"
#include "util.h"

#define ACCOUNT_FILE_MAX (1 << 20)

static void build_path(const struct container_files *f, const char *rel, char *out, size_t cap)
{
	snprintf(out, cap, "%s/%s", f->root, rel);
}

/* Rewrites one account file through build; a missing file starts empty. */
static int rewrite(const struct container_files *f, const char *rel, unsigned mode, char *(*build)(const char *, const void *),
		   const void *arg)
{
	static char old[ACCOUNT_FILE_MAX];
	char path[PATH_MAX];
	build_path(f, rel, path, sizeof path);
	if (read_text_file(path, old, sizeof old))
		old[0] = 0;
	char *text = build(old, arg);
	int failed = !text || save_text_file(path, text, mode);
	free(text);
	return failed ? -1 : 0;
}

struct account {
	const char *user, *shell, *replaced;
};

static char *build_passwd_text(const char *old, const void *arg)
{
	const struct account *a = arg;
	return build_passwd(old, a->user, a->shell);
}

static char *build_group_text(const char *old, const void *arg) { return build_group(old, ((const struct account *)arg)->user); }

static char *build_shadow_text(const char *old, const void *arg)
{
	const struct account *a = arg;
	return build_shadow(old, a->user, a->replaced);
}

static int write_account(const struct container_files *f)
{
	static char passwd[ACCOUNT_FILE_MAX];
	char path[PATH_MAX], replaced[64] = "";
	build_path(f, "etc/passwd", path, sizeof path);
	if (!read_text_file(path, passwd, sizeof passwd))
		find_user_with_uid(passwd, CONTAINER_USER_ID, replaced, sizeof replaced);
	build_path(f, "bin/bash", path, sizeof path);
	struct account a = { f->user, access(path, X_OK) ? "/bin/sh" : "/bin/bash", *replaced ? replaced : NULL };
	return rewrite(f, "etc/passwd", 0644, build_passwd_text, &a) || rewrite(f, "etc/group", 0644, build_group_text, &a) ||
	       rewrite(f, "etc/shadow", 0600, build_shadow_text, &a);
}

static void write_text(const struct container_files *f, const char *rel, const char *text, unsigned mode)
{
	char path[PATH_MAX];
	build_path(f, rel, path, sizeof path);
	save_text_file(path, text, mode);
}

static void write_sudo_rule(const struct container_files *f)
{
	char path[PATH_MAX], rule[128];
	build_path(f, "etc/sudoers.d", path, sizeof path);
	struct stat st;
	if (stat(path, &st) || !S_ISDIR(st.st_mode))
		return;
	snprintf(rule, sizeof rule, "%s ALL=(ALL:ALL) NOPASSWD: ALL\n", f->user);
	write_text(f, "etc/sudoers.d/pc-containerd", rule, 0440);
}

static void make_runtime_dir(const struct container_files *f, unsigned uid)
{
	char path[PATH_MAX];
	snprintf(path, sizeof path, "%s/run/user/%u", f->root, uid);
	make_dirs(path, 0700);
	chmod(path, 0700);
	if (chown(path, uid, uid))
		return; /* host trials map only uid 0 */
}

int write_container_files(const struct container_files *f)
{
	char text[512];
	int failed = write_account(f);
	write_sudo_rule(f);
	build_hosts(f->name, text, sizeof text);
	write_text(f, "etc/hosts", text, 0644);
	snprintf(text, sizeof text, "%s\n", f->name);
	write_text(f, "etc/hostname", text, 0644);
	if (f->resolv)
		write_text(f, "etc/resolv.conf", f->resolv, 0644);
	make_runtime_dir(f, 0);
	make_runtime_dir(f, CONTAINER_USER_ID);
	return failed;
}
