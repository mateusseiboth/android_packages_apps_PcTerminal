/*
 * Files the container's init writes inside the container at every start, after pivot_root (root is "")
 * or below a folder in the host tests: the person's account and sudo rule, /etc/hosts, /etc/hostname,
 * /etc/resolv.conf and /run/user/<uid>.
 */
#pragma once

struct container_files {
	const char *root;   /* "" inside the container */
	const char *name;   /* host name */
	const char *user;
	const char *resolv; /* text, or NULL to leave resolv.conf as it is */
};

/* -1 when the account files could not be written; the rest is best effort. */
int write_container_files(const struct container_files *f);
