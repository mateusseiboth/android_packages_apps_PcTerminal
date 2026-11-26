/*
 * pc-containerd: the Linux containers of LineageOS for PCs. Clients talk to it over a Unix socket with the
 * JSON lines of PROTOCOL.md; each connection is served by its own process.
 *
 *   pc-containerd                                    serve on init's socket (/dev/socket/pc-containerd)
 *   pc-containerd --socket PATH --root DIR [--rootless]
 *                                                    serve on PATH with DIR/{linux,media,wayland,cgroup} in
 *                                                    place of /data/linux, ... (host trials; README)
 */
#define _GNU_SOURCE
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include "ids.h"
#include "server.h"

#define LINE_MAX_BYTES 65536
#define FDS_MAX 4

/* ---- one connection -------------------------------------------------------------------- */

struct connection {
	int fd;
	unsigned uid;
	int fds[FDS_MAX]; /* received, not yet given to a line */
	int fd_count;
};

static void send_line(void *ctx, const char *line, int fd)
{
	struct connection *c = ctx;
	struct iovec iov = { (void *)line, strlen(line) };
	union {
		char buf[CMSG_SPACE(sizeof(int))];
		struct cmsghdr align;
	} control;
	struct msghdr msg = { .msg_iov = &iov, .msg_iovlen = 1 };
	if (fd >= 0) {
		msg.msg_control = control.buf;
		msg.msg_controllen = sizeof control.buf;
		struct cmsghdr *cm = CMSG_FIRSTHDR(&msg);
		cm->cmsg_level = SOL_SOCKET;
		cm->cmsg_type = SCM_RIGHTS;
		cm->cmsg_len = CMSG_LEN(sizeof(int));
		memcpy(CMSG_DATA(cm), &fd, sizeof fd);
	}
	while (iov.iov_len) {
		ssize_t n = sendmsg(c->fd, &msg, MSG_NOSIGNAL);
		if (n < 0 && errno == EINTR)
			continue;
		if (n <= 0)
			break;
		iov.iov_base = (char *)iov.iov_base + n;
		iov.iov_len -= (size_t)n;
		msg.msg_control = NULL;
		msg.msg_controllen = 0;
	}
	if (fd >= 0)
		close(fd);
}

static void keep_fds(struct connection *c, struct msghdr *msg)
{
	for (struct cmsghdr *cm = CMSG_FIRSTHDR(msg); cm; cm = CMSG_NXTHDR(msg, cm)) {
		if (cm->cmsg_level != SOL_SOCKET || cm->cmsg_type != SCM_RIGHTS)
			continue;
		size_t n = (cm->cmsg_len - CMSG_LEN(0)) / sizeof(int);
		for (size_t i = 0; i < n; i++) {
			int fd;
			memcpy(&fd, CMSG_DATA(cm) + i * sizeof(int), sizeof fd);
			if (c->fd_count < FDS_MAX)
				c->fds[c->fd_count++] = fd;
			else
				close(fd);
		}
	}
}

/* The first fd that came with the line's bytes; the line owns it now. */
static int take_fd(struct connection *c)
{
	if (!c->fd_count)
		return -1;
	int fd = c->fds[0];
	memmove(c->fds, c->fds + 1, (size_t)--c->fd_count * sizeof *c->fds);
	return fd;
}

static ssize_t receive(struct connection *c, char *buf, size_t cap)
{
	struct iovec iov = { buf, cap };
	union {
		char buf[CMSG_SPACE(sizeof(int) * FDS_MAX)];
		struct cmsghdr align;
	} control;
	struct msghdr msg = { .msg_iov = &iov, .msg_iovlen = 1, .msg_control = control.buf, .msg_controllen = sizeof control.buf };
	ssize_t n;
	do
		n = recvmsg(c->fd, &msg, MSG_CMSG_CLOEXEC);
	while (n < 0 && errno == EINTR);
	if (n > 0)
		keep_fds(c, &msg);
	return n;
}

static void answer_overlong(struct connection *c)
{
	send_line(c, "{\"id\":0,\"ok\":false,\"error\":\"bad_request\",\"message\":\"the line is too long\"}\n", -1);
}

static void serve_connection(struct server *s, struct connection *c)
{
	static char buf[LINE_MAX_BYTES + 1];
	size_t len = 0;
	int is_dropping = 0;
	ssize_t n;
	while ((n = receive(c, buf + len, LINE_MAX_BYTES - len)) > 0) {
		len += (size_t)n;
		buf[len] = 0;
		for (char *nl; (nl = memchr(buf, '\n', len));) {
			*nl = 0;
			int fd = take_fd(c);
			if (!is_dropping)
				handle_request(s, c->uid, buf, fd, send_line, c);
			else if (fd >= 0)
				close(fd);
			is_dropping = 0;
			len -= (size_t)(nl + 1 - buf);
			memmove(buf, nl + 1, len + 1);
		}
		if (len == LINE_MAX_BYTES) {
			if (!is_dropping)
				answer_overlong(c);
			is_dropping = 1;
			len = 0;
		}
	}
}

/* ---- accepting ------------------------------------------------------------------------- */

/* Root and the system app of any user (and, on a host trial, the daemon's own user). */
static int read_trusted_peer(int fd, unsigned *uid)
{
	struct ucred cred;
	socklen_t len = sizeof cred;
	if (getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &cred, &len))
		return -1;
	*uid = cred.uid;
	return is_trusted_uid(cred.uid) || cred.uid == getuid() ? 0 : -1;
}

static void serve_child(struct server *s, int listener, int fd, unsigned uid)
{
	struct connection c = { .fd = fd, .uid = uid };
	signal(SIGCHLD, SIG_DFL);
	close(listener);
	server_on_connect(s, uid);
	serve_connection(s, &c);
	_exit(0);
}

static int serve(int listener, struct server *s)
{
	signal(SIGCHLD, SIG_IGN); /* connection processes reap themselves */
	for (;;) {
		unsigned uid = 0;
		int fd = accept4(listener, NULL, NULL, SOCK_CLOEXEC);
		if (fd < 0 && (errno == EINTR || errno == ECONNABORTED))
			continue;
		if (fd < 0)
			return 1;
		if (!read_trusted_peer(fd, &uid) && fork() == 0)
			serve_child(s, listener, fd, uid);
		close(fd);
	}
}

/* init creates and binds /dev/socket/pc-containerd (pc-containerd.rc); listening is the service's job.
 * init publishes it with every non-alphanumeric character of the name as "_". */
static int open_init_socket(void)
{
	const char *env = getenv("ANDROID_SOCKET_pc_containerd");
	int fd = env ? atoi(env) : -1;
	return fd >= 0 && !listen(fd, 8) ? fd : -1;
}

static int open_path_socket(const char *path)
{
	struct sockaddr_un addr = { .sun_family = AF_UNIX };
	snprintf(addr.sun_path, sizeof addr.sun_path, "%s", path);
	unlink(path);
	int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
	if (fd < 0 || bind(fd, (struct sockaddr *)&addr, sizeof addr) || listen(fd, 8))
		return -1;
	return fd;
}

/* ---- host trials: --root DIR ----------------------------------------------------------- */

static struct sys_env build_root_env(const char *root, int is_rootless)
{
	static char paths[5][512];
	static const char *const RELS[] = { "linux", "media", "wayland", "cgroup", "packages.list" };
	for (int i = 0; i < 5; i++)
		snprintf(paths[i], sizeof paths[i], "%s/%s", root, RELS[i]);
	return (struct sys_env){ paths[0], paths[1], paths[2], paths[3], "/proc", "/dev", paths[4], is_rootless, NULL };
}

static const char *find_option(int argc, char **argv, const char *name)
{
	for (int i = 1; i + 1 < argc; i++)
		if (!strcmp(argv[i], name))
			return argv[i + 1];
	return NULL;
}

static int has_flag(int argc, char **argv, const char *name)
{
	for (int i = 1; i < argc; i++)
		if (!strcmp(argv[i], name))
			return 1;
	return 0;
}

int main(int argc, char **argv)
{
	static struct server s;
	static struct sys_env root_env;
	const char *path = find_option(argc, argv, "--socket");
	const char *root = find_option(argc, argv, "--root");
	int is_rootless = has_flag(argc, argv, "--rootless");
	const struct sys_env *env = linux_sys_env();
	if (root) {
		root_env = build_root_env(root, is_rootless);
		env = &root_env;
	}
	int listener = path ? open_path_socket(path) : open_init_socket();
	if (listener < 0) {
		fprintf(stderr, "pc-containerd: no socket (%s)\n", strerror(errno));
		return 1;
	}
	signal(SIGPIPE, SIG_IGN);
	server_init(&s, env, is_rootless ? linux_rootless_sys_ops() : linux_sys_ops());
	return serve(listener, &s);
}
