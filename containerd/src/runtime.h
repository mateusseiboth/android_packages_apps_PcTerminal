/* Pieces of starting and entering a container that need no machine. */
#pragma once

#define EXEC_ENV_MAX 96

/* Host uid of MediaProvider for this Android user (packages.list): the owner of the person's files. 0 if unknown. */
unsigned find_storage_uid(const char *packages_list, unsigned user);

struct env_input {
	const char *user;
	unsigned uid;
	const char *home;
	const char *shell;
	int use_pty;
};

struct exec_env {
	char *vars[EXEC_ENV_MAX + 1]; /* NULL-terminated, pointing into buf */
	int count;
	char buf[16384];
};

/* The defaults of PROTOCOL.md ("exec") with extra names set over them; -1 on a bad name or no room. */
int build_exec_env(struct exec_env *e, const struct env_input *in, const char *const *keys, const char *const *values, int count);
