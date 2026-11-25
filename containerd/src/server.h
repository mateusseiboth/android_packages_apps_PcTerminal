/* pc-containerd's requests (PROTOCOL.md): one JSON line in, events and one reply line out. */
#pragma once
#include "sysops.h"

#define PROTOCOL_VERSION 1

struct server {
	const struct sys_env *env;
	const struct sys_ops *ops;
};

/* A reply or event line (ending in '\n') and the fd to attach to it, or -1. The callee owns the fd. */
typedef void (*reply_fn)(void *ctx, const char *line, int fd);

void server_init(struct server *s, const struct sys_env *env, const struct sys_ops *ops);
/* A new connection: the system app of a user (the compositor at login) gets that user's Wayland folder
 * (PROTOCOL.md, "Container layout") before its first request. */
void server_on_connect(struct server *s, unsigned peer_uid);
/* peer_uid picks the Android user; fd is what came attached to the line (-1 when none) and is always
 * closed before this returns. */
void handle_request(struct server *s, unsigned peer_uid, const char *line, int fd, reply_fn reply, void *ctx);
