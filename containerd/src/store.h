/* Containers on disk: <data_root>/<userId>/<name>/{rootfs,config.json} (PROTOCOL.md, "Container layout"). */
#pragma once
#include <stddef.h>

struct container {
	char name[32];
	char distro[64];
	char release[64];
	char user[33];
	long long created;
	unsigned id_base;
};

struct store {
	const char *data_root; /* /data/linux */
};

int save_container_config(const char *path, const struct container *c);
/* Fills every field but name; -1 when missing or not valid. */
int read_container_config(const char *path, struct container *c);

/* <data_root>/<user>/<name>/<leaf> ("" for the container folder). */
void build_container_path(const struct store *st, unsigned user, const char *name, const char *leaf, char *out, size_t cap);
/* Makes <user>/<name>/rootfs; -1 when the container folder already exists or the name is not valid. */
int add_container_dirs(const struct store *st, unsigned user, const char *name);
int save_container(const struct store *st, unsigned user, const struct container *c);
/* The containers with a readable config.json, sorted by name; at most max. */
int list_containers(const struct store *st, unsigned user, struct container *out, int max);
int find_container(const struct store *st, unsigned user, const char *name, struct container *out);
int rename_container(const struct store *st, unsigned user, const char *name, const char *new_name);
/* Removes the container folder; symbolic links are removed, never followed, and other mounts are not entered. */
int remove_container(const struct store *st, unsigned user, const char *name);
