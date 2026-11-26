/*
 * A container's cgroup v2 group, <cgroup root>/pc-containers/u<userId>-<name>: it holds every process of
 * the container, so it is also how the daemon knows a container runs and finds its init.
 */
#pragma once
#include <stddef.h>

struct cgroup_stats {
	int has_memory, has_cpu, has_pids;
	unsigned long long memory_bytes, cpu_usec, pids;
};

void build_cgroup_dir(const char *cgroup_root, unsigned user, const char *name, char *out, size_t cap);
/* usage_usec of a cpu.stat text. */
int parse_cpu_usage(const char *text, unsigned long long *usec);
/* The group's process whose pid inside its pid namespace is 1; -1 when none (the container is stopped). */
int find_init_pid(const char *cgroup_dir, const char *proc);
/* The base name of argv[0] of each process of the group (cgroup.procs order), each followed by '\n', into
 * out; the number of names, or -1 when the group cannot be read. Names that do not fit are left out. */
int list_group_programs(const char *cgroup_dir, const char *proc, char *out, size_t cap);
/* The pid of a host process inside its own pid namespace (last NSpid field); -1 when unknown. */
int find_ns_pid(const char *proc, int pid);
void read_cgroup_stats(const char *cgroup_dir, struct cgroup_stats *s);
/* Bytes of disk the files below dir use (allocated blocks), hard links counted once, links not followed. */
unsigned long long find_disk_usage(const char *dir);
