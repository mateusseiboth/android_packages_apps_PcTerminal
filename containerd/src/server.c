#define _GNU_SOURCE
#include "server.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "accounts.h"
#include "cgroup.h"
#include "desktop.h"
#include "ids.h"
#include "inroot.h"
#include "json.h"
#include "mounts.h"
#include "runtime.h"
#include "store.h"
#include "tar.h"
#include "util.h"

#define CONTAINERS_MAX 64
#define ARGV_MAX 64
#define DNS_MAX 3
#define PROGRESS_EVERY_MS 250
#define STOP_WAIT_STEPS 70 /* of STEP_MS: the init gives its processes 5 s */
#define KILL_WAIT_STEPS 20
#define STEP_MS 100
#define TERMINAL_MAX 1000

struct request {
	struct server *s;
	const struct json *j;
	long long id;
	unsigned user;
	int fd; /* attached to the request; closed after the handler unless it took it (-1) */
	reply_fn reply;
	void *ctx;
	struct store st;
	char message[256];
	long long last_progress_ms;
};

void server_init(struct server *s, const struct sys_env *env, const struct sys_ops *ops)
{
	*s = (struct server){ env, ops };
}

/* ---- replies ---------------------------------------------------------------------------- */

static const char *const DEFAULT_MESSAGES[][2] = {
	{ "bad_request", "the request is not valid" },  { "bad_name", "the container name is not valid" },
	{ "bad_user", "the user is not valid" },	  { "not_found", "no such container" },
	{ "exists", "a container with that name exists" }, { "running", "the container is running" },
	{ "no_fd", "the request needs an fd" },	  { "bad_archive", "the stream is not a tar archive" },
	{ "no_space", "the disk is full" },		  { "start_failed", "the container could not start" },
	{ "exec_failed", "the command could not run" },	  { "no_icon", "no icon file" },
	{ "io", "the daemon could not finish" },
};

static const char *find_default_message(const char *code)
{
	for (size_t i = 0; i < sizeof DEFAULT_MESSAGES / sizeof *DEFAULT_MESSAGES; i++)
		if (!strcmp(DEFAULT_MESSAGES[i][0], code))
			return DEFAULT_MESSAGES[i][1];
	return code;
}

static void begin_reply(const struct request *q, struct jw *w)
{
	jw_init(w);
	jw_begin_object(w);
	jw_key_int(w, "id", q->id);
	jw_key_bool(w, "ok", 1);
}

static void send_reply(const struct request *q, struct jw *w, int fd)
{
	jw_end_object(w);
	q->reply(q->ctx, jw_line(w), fd);
	jw_free(w);
}

static void send_ok(const struct request *q)
{
	struct jw w;
	begin_reply(q, &w);
	send_reply(q, &w, -1);
}

static void send_error(const struct request *q, const char *code)
{
	struct jw w;
	jw_init(&w);
	jw_begin_object(&w);
	jw_key_int(&w, "id", q->id);
	jw_key_bool(&w, "ok", 0);
	jw_key_string(&w, "error", code);
	jw_key_string(&w, "message", *q->message ? q->message : find_default_message(code));
	send_reply(q, &w, -1);
}

static const char *fail(struct request *q, const char *code, const char *message)
{
	snprintf(q->message, sizeof q->message, "%s", message);
	return code;
}

/* ---- containers ------------------------------------------------------------------------- */

static void build_cgroup_path(const struct request *q, const char *name, char *out, size_t cap)
{
	build_cgroup_dir(q->s->env->cgroup_root, q->user, name, out, cap);
}

static int find_running_init(const struct request *q, const char *name)
{
	char dir[PATH_MAX];
	build_cgroup_path(q, name, dir, sizeof dir);
	return find_init_pid(dir, q->s->env->proc);
}

static const char *find_named(struct request *q, const char *key, struct container *c)
{
	const char *name = json_string(q->j, key);
	if (!name)
		return fail(q, "bad_request", "the request needs a name");
	return find_container(&q->st, q->user, name, c) ? "not_found" : NULL;
}

static void write_container(struct jw *w, const struct container *c, int running)
{
	jw_begin_object(w);
	jw_key_string(w, "name", c->name);
	jw_key_string(w, "distro", c->distro);
	jw_key_string(w, "release", c->release);
	jw_key_int(w, "created", c->created);
	jw_key_string(w, "user", c->user);
	jw_key_string(w, "state", running ? "running" : "stopped");
	jw_end_object(w);
}

static void send_container(const struct request *q, const struct container *c)
{
	struct jw w;
	begin_reply(q, &w);
	jw_key(&w, "container");
	write_container(&w, c, find_running_init(q, c->name) > 0);
	send_reply(q, &w, -1);
}

/* ---- starting and stopping ------------------------------------------------------------- */

/* Memory and pids accounting for the containers' groups; Android may have enabled some already. */
static void prepare_cgroup(const struct request *q, const char *dir)
{
	char parent[PATH_MAX], control[PATH_MAX];
	snprintf(parent, sizeof parent, "%s/pc-containers", q->s->env->cgroup_root);
	mkdir(parent, 0755);
	static const char *const CONTROLLERS[] = { "+memory", "+pids" };
	const char *const LEVELS[] = { q->s->env->cgroup_root, parent };
	for (size_t i = 0; i < 2; i++)
		for (size_t k = 0; k < 2; k++) {
			snprintf(control, sizeof control, "%s/cgroup.subtree_control", LEVELS[i]);
			int fd = open(control, O_WRONLY | O_CLOEXEC);
			if (fd >= 0 && write(fd, CONTROLLERS[k], strlen(CONTROLLERS[k])) < 0)
				errno = 0;
			if (fd >= 0)
				close(fd);
		}
	mkdir(dir, 0755);
}

/* /data/misc/pc-wayland/<user>: made root 0711 when missing, never changed when it exists. */
static void prepare_wayland_dir(const struct request *q)
{
	char dir[PATH_MAX];
	snprintf(dir, sizeof dir, "%s/%u", q->s->env->wayland_root, q->user);
	if (mkdir(q->s->env->wayland_root, 0711) && errno != EEXIST)
		return;
	if (!mkdir(dir, 0711))
		chmod(dir, 0711);
}

static unsigned read_storage_uid(const struct request *q)
{
	static char text[1 << 20];
	return read_text_file(q->s->env->packages_list, text, sizeof text) ? 0 : find_storage_uid(text, q->user);
}

static const char *start_container(struct request *q, const struct container *c, const char *resolv)
{
	char cgroup_dir[PATH_MAX], rootfs[PATH_MAX];
	struct mount_step plan[MOUNT_STEPS_MAX];
	const struct sys_env *env = q->s->env;
	struct plan_input in = { env->dev, env->media_root, env->wayland_root, q->user, c->user };
	int count = build_mount_plan(&in, plan, MOUNT_STEPS_MAX), pid = 0;
	build_cgroup_path(q, c->name, cgroup_dir, sizeof cgroup_dir);
	build_container_path(&q->st, q->user, c->name, "rootfs", rootfs, sizeof rootfs);
	prepare_cgroup(q, cgroup_dir);
	prepare_wayland_dir(q);
	struct start_spec spec = { c->name, rootfs, cgroup_dir, c->user, c->id_base, read_storage_uid(q), plan, count, resolv };
	if (count < 0 || q->s->ops->start_init(env, &spec, &pid)) {
		rmdir(cgroup_dir);
		return "start_failed";
	}
	return NULL;
}

static int is_cgroup_empty(const char *dir)
{
	char path[PATH_MAX + 32], text[64];
	snprintf(path, sizeof path, "%s/cgroup.procs", dir);
	return read_text_file(path, text, sizeof text) || !*text;
}

static int wait_until_empty(const struct request *q, const char *dir, int steps)
{
	for (int i = 0; i < steps && !is_cgroup_empty(dir); i++)
		q->s->ops->sleep_ms(STEP_MS);
	return is_cgroup_empty(dir);
}

/* cgroup.kill (Linux 5.14) ends every process at once; the init's SIGKILL ends its pid namespace. */
static void kill_group(const struct request *q, const char *dir, int init)
{
	char path[PATH_MAX + 32];
	snprintf(path, sizeof path, "%s/cgroup.kill", dir);
	if (save_text_file(path, "1", 0644) && init > 0)
		q->s->ops->send_signal(init, SIGKILL);
}

static void stop_container(struct request *q, const struct container *c)
{
	char dir[PATH_MAX];
	build_cgroup_path(q, c->name, dir, sizeof dir);
	int init = find_init_pid(dir, q->s->env->proc);
	if (init > 0)
		q->s->ops->send_signal(init, SIGTERM);
	if (!wait_until_empty(q, dir, STOP_WAIT_STEPS)) {
		kill_group(q, dir, init);
		wait_until_empty(q, dir, KILL_WAIT_STEPS);
	}
	rmdir(dir);
}

/* ---- handlers --------------------------------------------------------------------------- */

static const char *handle_version(struct request *q)
{
	struct jw w;
	begin_reply(q, &w);
	jw_key_int(&w, "protocol", PROTOCOL_VERSION);
	send_reply(q, &w, -1);
	return NULL;
}

static const char *handle_list(struct request *q)
{
	struct container list[CONTAINERS_MAX];
	int n = list_containers(&q->st, q->user, list, CONTAINERS_MAX);
	struct jw w;
	begin_reply(q, &w);
	jw_key(&w, "containers");
	jw_begin_array(&w);
	for (int i = 0; i < n; i++)
		write_container(&w, &list[i], find_running_init(q, list[i].name) > 0);
	jw_end_array(&w);
	send_reply(q, &w, -1);
	return NULL;
}

/* ---- create ----------------------------------------------------------------------------- */

static int is_label(const char *s)
{
	size_t len = s ? strlen(s) : 0;
	if (!len || len > 63)
		return 0;
	for (size_t i = 0; i < len; i++)
		if (!strchr("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789._-", s[i]))
			return 0;
	return 1;
}

static long long read_clock_ms(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static void send_progress(void *ctx, const struct tar_progress *p)
{
	struct request *q = ctx;
	long long now = read_clock_ms();
	if (p->files > 1 && now - q->last_progress_ms < PROGRESS_EVERY_MS)
		return;
	q->last_progress_ms = now;
	struct jw w;
	jw_init(&w);
	jw_begin_object(&w);
	jw_key_int(&w, "id", q->id);
	jw_key_string(&w, "event", "progress");
	jw_key_int(&w, "bytes", (long long)p->bytes);
	jw_key_int(&w, "files", (long long)p->files);
	send_reply(q, &w, -1);
}

static int chown_entry(void *ctx, int dirfd, const char *name, unsigned uid, unsigned gid)
{
	struct request *q = ctx;
	return q->s->ops->chown_at(NULL, dirfd, name, uid, gid);
}

static const char *check_create(struct request *q, struct container *c)
{
	const char *name = json_string(q->j, "name"), *user = json_string(q->j, "user");
	const char *distro = json_string(q->j, "distro"), *release = json_string(q->j, "release");
	if (!is_valid_name(name))
		return "bad_name";
	if (!is_valid_user_name(user ? user : "user"))
		return "bad_user";
	if (!is_label(distro) || !is_label(release))
		return fail(q, "bad_request", "distro and release are needed");
	if (q->fd < 0)
		return "no_fd";
	*c = (struct container){ .created = (long long)time(NULL), .id_base = find_host_id_base(q->user) };
	snprintf(c->name, sizeof c->name, "%s", name);
	snprintf(c->distro, sizeof c->distro, "%s", distro);
	snprintf(c->release, sizeof c->release, "%s", release);
	snprintf(c->user, sizeof c->user, "%s", user ? user : "user");
	return NULL;
}

static const char *extract_rootfs(struct request *q, const struct container *c)
{
	char rootfs[PATH_MAX];
	build_container_path(&q->st, q->user, c->name, "rootfs", rootfs, sizeof rootfs);
	int root = open(rootfs, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
	if (root < 0)
		return "io";
	struct tar_target t = { root, c->id_base, chown_entry, q, send_progress };
	const char *error = extract_tar(q->fd, &t, NULL);
	if (!error && q->s->ops->chown_at(NULL, root, "", c->id_base, c->id_base))
		error = "io";
	close(root);
	return error;
}

static const char *handle_create(struct request *q)
{
	struct container c, existing;
	const char *error = check_create(q, &c);
	if (error)
		return error;
	if (!find_container(&q->st, q->user, c.name, &existing))
		return "exists";
	if (add_container_dirs(&q->st, q->user, c.name))
		return errno == EEXIST ? "exists" : "io";
	error = extract_rootfs(q, &c);
	error = error ? error : save_container(&q->st, q->user, &c) ? "io" : NULL;
	if (error) {
		remove_container(&q->st, q->user, c.name);
		return error;
	}
	send_container(q, &c);
	return NULL;
}

/* ---- start, stop, delete, rename -------------------------------------------------------- */

static const char *build_resolv(struct request *q, char *out, size_t cap, const char **resolv)
{
	const struct json *dns = json_find(q->j, "dns");
	const char *servers[DNS_MAX];
	size_t n = json_count(dns);
	*resolv = NULL;
	if (!dns)
		return NULL;
	if (dns->type != JSON_ARRAY || n > DNS_MAX)
		return fail(q, "bad_request", "dns is a list of at most 3 addresses");
	for (size_t i = 0; i < n; i++) {
		servers[i] = json_at(dns, i)->type == JSON_STRING ? json_at(dns, i)->str : "";
	}
	if (build_resolv_conf(servers, (int)n, out, cap))
		return fail(q, "bad_request", "dns holds something that is not an address");
	*resolv = out;
	return NULL;
}

static void send_state(const struct request *q, const char *state)
{
	struct jw w;
	begin_reply(q, &w);
	jw_key_string(&w, "state", state);
	send_reply(q, &w, -1);
}

static const char *handle_start(struct request *q)
{
	struct container c;
	char text[512];
	const char *resolv = NULL;
	const char *error = find_named(q, "name", &c);
	if (!error && find_running_init(q, c.name) < 0) {
		error = build_resolv(q, text, sizeof text, &resolv);
		error = error ? error : start_container(q, &c, resolv);
	}
	if (error)
		return error;
	send_state(q, "running");
	return NULL;
}

static const char *handle_stop(struct request *q)
{
	struct container c;
	const char *error = find_named(q, "name", &c);
	if (error)
		return error;
	stop_container(q, &c);
	send_state(q, "stopped");
	return NULL;
}

static const char *handle_delete(struct request *q)
{
	struct container c;
	const char *error = find_named(q, "name", &c);
	if (error)
		return error;
	stop_container(q, &c);
	if (remove_container(&q->st, q->user, c.name))
		return "io";
	send_ok(q);
	return NULL;
}

static const char *handle_rename(struct request *q)
{
	struct container c, renamed;
	const char *new_name = json_string(q->j, "new_name");
	const char *error = find_named(q, "name", &c);
	if (error)
		return error;
	if (!is_valid_name(new_name))
		return "bad_name";
	if (find_running_init(q, c.name) > 0)
		return "running";
	if (rename_container(&q->st, q->user, c.name, new_name))
		return errno == EEXIST || errno == ENOTEMPTY ? "exists" : "io";
	if (find_container(&q->st, q->user, new_name, &renamed))
		return "io";
	send_container(q, &renamed);
	return NULL;
}

/* ---- exec ------------------------------------------------------------------------------- */

struct exec_request {
	char *argv[ARGV_MAX + 1];
	char shell[256], home[64], cwd[PATH_MAX];
	const char *account;
	unsigned uid;
	int use_pty, rows, cols;
	struct exec_env env;
};

static int read_terminal_size(const struct request *q, int *rows, int *cols)
{
	long long r = 24, c = 80;
	if ((json_find(q->j, "rows") && json_int(q->j, "rows", &r)) || (json_find(q->j, "cols") && json_int(q->j, "cols", &c)))
		return -1;
	if (r < 1 || r > TERMINAL_MAX || c < 1 || c > TERMINAL_MAX)
		return -1;
	*rows = (int)r;
	*cols = (int)c;
	return 0;
}

static const char *read_account(struct request *q, const struct container *c, struct exec_request *x)
{
	const char *user = json_string(q->j, "user");
	if (json_find(q->j, "user") && !user)
		return "bad_user";
	int is_root = user && !strcmp(user, "root");
	if (user && !is_root && strcmp(user, c->user))
		return "bad_user";
	x->account = is_root ? "root" : c->user;
	x->uid = is_root ? 0 : CONTAINER_USER_ID;
	snprintf(x->home, sizeof x->home, is_root ? "/root" : "/home/%s", c->user);
	return NULL;
}

/* The account's login shell from the container's /etc/passwd, else /bin/sh. */
static void read_shell(struct request *q, const struct container *c, struct exec_request *x)
{
	static char passwd[1 << 16];
	char rootfs[PATH_MAX];
	build_container_path(&q->st, q->user, c->name, "rootfs", rootfs, sizeof rootfs);
	int root = open(rootfs, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
	int fd = root >= 0 ? open_in_root(root, "etc/passwd", O_RDONLY | O_NONBLOCK, 0) : -1;
	ssize_t n = fd >= 0 ? read(fd, passwd, sizeof passwd - 1) : -1;
	passwd[n > 0 ? n : 0] = 0;
	if (fd >= 0)
		close(fd);
	if (root >= 0)
		close(root);
	if (find_login_shell(passwd, x->account, x->shell, sizeof x->shell) || x->shell[0] != '/')
		snprintf(x->shell, sizeof x->shell, "/bin/sh");
}

static const char *read_argv(struct request *q, struct exec_request *x)
{
	static char login_flag[] = "-l";
	const struct json *argv = json_find(q->j, "argv");
	size_t n = json_count(argv);
	if (!argv) {
		x->argv[0] = x->shell;
		x->argv[1] = login_flag;
		x->argv[2] = NULL;
		return NULL;
	}
	if (argv->type != JSON_ARRAY || n < 1 || n > ARGV_MAX)
		return fail(q, "bad_request", "argv is a list of 1 to 64 strings");
	for (size_t i = 0; i < n; i++) {
		if (json_at(argv, i)->type != JSON_STRING)
			return fail(q, "bad_request", "argv is a list of 1 to 64 strings");
		x->argv[i] = json_at(argv, i)->str;
	}
	x->argv[n] = NULL;
	return NULL;
}

static const char *read_env(struct request *q, struct exec_request *x)
{
	const struct json *env = json_find(q->j, "env");
	const char *keys[EXEC_ENV_MAX], *values[EXEC_ENV_MAX];
	int n = 0;
	if (env && env->type != JSON_OBJECT)
		return fail(q, "bad_request", "env is an object of strings");
	for (const struct json *m = env ? env->child : NULL; m; m = m->next) {
		if (m->type != JSON_STRING || n == EXEC_ENV_MAX)
			return fail(q, "bad_request", "env is an object of strings");
		keys[n] = m->key;
		values[n++] = m->str;
	}
	struct env_input in = { x->account, x->uid, x->home, x->shell, x->use_pty };
	return build_exec_env(&x->env, &in, keys, values, n) ? fail(q, "bad_request", "env has a name that is not valid") : NULL;
}

static const char *read_exec(struct request *q, const struct container *c, struct exec_request *x)
{
	const char *cwd = json_string(q->j, "cwd");
	const char *error = read_account(q, c, x);
	if (error)
		return error;
	x->use_pty = json_bool(q->j, "pty", 0);
	if (read_terminal_size(q, &x->rows, &x->cols))
		return fail(q, "bad_request", "rows and cols go from 1 to 1000");
	read_shell(q, c, x);
	snprintf(x->cwd, sizeof x->cwd, "%s", cwd && cwd[0] == '/' ? cwd : x->home);
	error = read_argv(q, x);
	return error ? error : read_env(q, x);
}

static void send_exec_reply(const struct request *q, int pid, int pty)
{
	struct jw w;
	begin_reply(q, &w);
	jw_key_int(&w, "pid", pid);
	if (pty >= 0)
		jw_key_bool(&w, "fd", 1);
	send_reply(q, &w, pty);
}

static const char *handle_exec(struct request *q)
{
	struct container c;
	static struct exec_request x;
	char cgroup_dir[PATH_MAX];
	int pty = -1, pid = 0;
	const char *error = find_named(q, "name", &c);
	error = error ? error : read_exec(q, &c, &x);
	if (!error && find_running_init(q, c.name) < 0)
		error = start_container(q, &c, NULL);
	if (error)
		return error;
	build_cgroup_path(q, c.name, cgroup_dir, sizeof cgroup_dir);
	struct exec_spec spec = { find_running_init(q, c.name), cgroup_dir, x.argv, x.env.vars, x.cwd, x.uid, x.uid,
				  x.use_pty, x.rows, x.cols };
	if (spec.init_pid < 0)
		return "start_failed";
	if (q->s->ops->exec_in(q->s->env, &spec, &pty, &pid, q->message, sizeof q->message))
		return "exec_failed";
	send_exec_reply(q, pid, pty);
	return NULL;
}

static const char *handle_resize(struct request *q)
{
	int rows = 0, cols = 0;
	if (q->fd < 0)
		return "no_fd";
	if (!json_find(q->j, "rows") || !json_find(q->j, "cols") || read_terminal_size(q, &rows, &cols))
		return fail(q, "bad_request", "rows and cols go from 1 to 1000");
	if (q->s->ops->set_window_size(q->fd, rows, cols))
		return "io";
	send_ok(q);
	return NULL;
}

/* ---- apps, icon, stats ------------------------------------------------------------------ */

static int open_rootfs(struct request *q, const struct container *c)
{
	char rootfs[PATH_MAX];
	build_container_path(&q->st, q->user, c->name, "rootfs", rootfs, sizeof rootfs);
	return open(rootfs, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
}

static void write_categories(struct jw *w, const char *categories)
{
	char copy[512];
	snprintf(copy, sizeof copy, "%s", categories);
	jw_key(w, "categories");
	jw_begin_array(w);
	for (char *save = NULL, *c = strtok_r(copy, ";", &save); c; c = strtok_r(NULL, ";", &save))
		jw_string(w, c);
	jw_end_array(w);
}

static void write_app(struct jw *w, const struct desktop_app *a)
{
	jw_begin_object(w);
	jw_key_string(w, "id", a->id);
	jw_key_string(w, "name", a->name);
	jw_key_string(w, "exec", a->exec);
	jw_key(w, "argv");
	jw_begin_array(w);
	for (int i = 0; i < a->argv.argc; i++)
		jw_string(w, a->argv.argv[i]);
	jw_end_array(w);
	jw_key_string(w, "icon", a->icon);
	jw_key_string_or_null(w, "icon_path", *a->icon_path ? a->icon_path : NULL);
	jw_key_string_or_null(w, "icon_format", a->icon_format);
	jw_key_bool(w, "terminal", a->terminal);
	write_categories(w, a->categories);
	jw_end_object(w);
}

static const char *handle_apps(struct request *q)
{
	struct container c;
	struct desktop_app *apps = NULL;
	const char *error = find_named(q, "name", &c);
	int root = error ? -1 : open_rootfs(q, &c);
	if (error || root < 0)
		return error ? error : "io";
	int n = list_apps(root, json_string(q->j, "locale"), &apps);
	close(root);
	struct jw w;
	begin_reply(q, &w);
	jw_key(&w, "apps");
	jw_begin_array(&w);
	for (int i = 0; i < n; i++)
		write_app(&w, &apps[i]);
	jw_end_array(&w);
	free(apps);
	send_reply(q, &w, -1);
	return NULL;
}

/* The icon path of the app with that desktop id; "" when it has none. */
static void find_icon_path(int root, const char *app, char *out, size_t cap)
{
	struct desktop_app *apps = NULL;
	int n = list_apps(root, NULL, &apps);
	*out = 0;
	for (int i = 0; i < n; i++)
		if (!strcmp(apps[i].id, app))
			snprintf(out, cap, "%s", apps[i].icon_path);
	free(apps);
}

static const char *handle_icon(struct request *q)
{
	struct container c;
	char path[512];
	const char *app = json_string(q->j, "app"), *given = json_string(q->j, "path"), *format = NULL;
	const char *error = find_named(q, "name", &c);
	if (!error && !app && !given)
		error = fail(q, "bad_request", "the request needs app or path");
	int root = error ? -1 : open_rootfs(q, &c), size = 0;
	if (error || root < 0)
		return error ? error : "io";
	if (app)
		find_icon_path(root, app, path, sizeof path);
	else
		snprintf(path, sizeof path, "%s", given);
	int fd = *path ? open_icon(root, path, &format, &size) : -1;
	close(root);
	if (fd < 0)
		return "no_icon";
	struct jw w;
	begin_reply(q, &w);
	jw_key_bool(&w, "fd", 1);
	jw_key_string(&w, "format", format);
	jw_key_int(&w, "size", size);
	send_reply(q, &w, fd);
	return NULL;
}

static void write_stat(struct jw *w, const char *key, int has, unsigned long long value)
{
	if (has)
		jw_key_int(w, key, (long long)value);
	else
		jw_key_null(w, key);
}

static const char *handle_stats(struct request *q)
{
	struct container c;
	struct cgroup_stats s = { 0 };
	char dir[PATH_MAX], rootfs[PATH_MAX];
	const char *error = find_named(q, "name", &c);
	if (error)
		return error;
	int running = find_running_init(q, c.name) > 0;
	build_cgroup_path(q, c.name, dir, sizeof dir);
	if (running)
		read_cgroup_stats(dir, &s);
	build_container_path(&q->st, q->user, c.name, "rootfs", rootfs, sizeof rootfs);
	struct jw w;
	begin_reply(q, &w);
	jw_key_string(&w, "state", running ? "running" : "stopped");
	write_stat(&w, "memory_bytes", s.has_memory, s.memory_bytes);
	write_stat(&w, "cpu_usec", s.has_cpu, s.cpu_usec);
	write_stat(&w, "pids", s.has_pids, s.pids);
	jw_key_int(&w, "disk_bytes", (long long)find_disk_usage(rootfs));
	send_reply(q, &w, -1);
	return NULL;
}

/* ---- dispatch --------------------------------------------------------------------------- */

static const struct {
	const char *cmd;
	const char *(*handle)(struct request *);
} HANDLERS[] = {
	{ "version", handle_version }, { "list", handle_list },	    { "create", handle_create }, { "start", handle_start },
	{ "stop", handle_stop },       { "delete", handle_delete }, { "rename", handle_rename }, { "exec", handle_exec },
	{ "resize", handle_resize },   { "apps", handle_apps },	    { "icon", handle_icon },	 { "stats", handle_stats },
};

static const char *dispatch(struct request *q)
{
	const char *cmd = json_string(q->j, "cmd");
	if (!q->j)
		return fail(q, "bad_request", "not a JSON request");
	if (!cmd || json_int(q->j, "id", &q->id))
		return fail(q, "bad_request", "the request needs an integer id and a cmd");
	for (size_t i = 0; i < sizeof HANDLERS / sizeof *HANDLERS; i++)
		if (!strcmp(HANDLERS[i].cmd, cmd))
			return HANDLERS[i].handle(q);
	return fail(q, "bad_request", "unknown command");
}

void handle_request(struct server *s, unsigned peer_uid, const char *line, int fd, reply_fn reply, void *ctx)
{
	struct json *j = json_parse(line);
	struct request q = { .s = s, .j = j, .user = find_android_user(peer_uid), .fd = fd, .reply = reply, .ctx = ctx,
			     .st = { s->env->data_root } };
	const char *error = dispatch(&q);
	if (error)
		send_error(&q, error);
	if (q.fd >= 0)
		close(q.fd);
	json_free(j);
}
