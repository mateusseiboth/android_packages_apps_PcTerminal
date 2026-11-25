#define _GNU_SOURCE
#include "mounts.h"

#include <stdio.h>
#include <string.h>
#include <sys/mount.h>

struct plan {
	struct mount_step *steps;
	int max, count;
};

static struct mount_step *add(struct plan *p, enum mount_kind kind, const char *source, const char *target)
{
	if (p->count >= p->max) {
		p->count = p->max + 1;
		return NULL;
	}
	struct mount_step *s = &p->steps[p->count++];
	*s = (struct mount_step){ .kind = kind };
	snprintf(s->source, sizeof s->source, "%s", source ? source : "");
	snprintf(s->target, sizeof s->target, "%s", target);
	return s;
}

static void add_tmpfs(struct plan *p, const char *target, const char *options, unsigned long flags)
{
	struct mount_step *s = add(p, MOUNT_TMPFS, "tmpfs", target);
	if (!s)
		return;
	snprintf(s->options, sizeof s->options, "%s", options);
	s->flags = flags;
}

/* The host device nodes every Linux program expects; the container cannot make its own. */
static const char *const DEVICE_NODES[] = { "null", "zero", "full", "random", "urandom", "tty" };

/* Shared with the host when present: GPU render nodes, sound cards, FUSE. */
static const struct {
	const char *name;
	enum mount_kind kind;
	int is_file;
} HOST_DEVICES[] = { { "dri", MOUNT_RBIND, 0 }, { "snd", MOUNT_RBIND, 0 }, { "fuse", MOUNT_BIND, 1 } };

static const char *const DEV_LINKS[][2] = {
	{ "/dev/ptmx", "pts/ptmx" },	    { "/dev/fd", "/proc/self/fd" },	  { "/dev/stdin", "/proc/self/fd/0" },
	{ "/dev/stdout", "/proc/self/fd/1" }, { "/dev/stderr", "/proc/self/fd/2" },
};

static void add_dev(struct plan *p, const struct plan_input *in)
{
	char source[256], target[128];
	add_tmpfs(p, "/dev", "mode=755,size=65536k", MS_NOSUID | MS_NOEXEC);
	for (size_t i = 0; i < sizeof DEVICE_NODES / sizeof *DEVICE_NODES; i++) {
		snprintf(source, sizeof source, "%s/%s", in->dev, DEVICE_NODES[i]);
		snprintf(target, sizeof target, "/dev/%s", DEVICE_NODES[i]);
		struct mount_step *s = add(p, MOUNT_BIND, source, target);
		if (s)
			s->is_file = 1;
	}
	struct mount_step *pts = add(p, MOUNT_DEVPTS, "devpts", "/dev/pts");
	if (pts) {
		snprintf(pts->options, sizeof pts->options, "newinstance,ptmxmode=0666,mode=0620,gid=5");
		pts->flags = MS_NOSUID | MS_NOEXEC;
	}
	for (size_t i = 0; i < sizeof DEV_LINKS / sizeof *DEV_LINKS; i++)
		add(p, MOUNT_SYMLINK, DEV_LINKS[i][1], DEV_LINKS[i][0]);
	add_tmpfs(p, "/dev/shm", "mode=1777", MS_NOSUID | MS_NODEV);
	struct mount_step *mq = add(p, MOUNT_MQUEUE, "mqueue", "/dev/mqueue");
	if (mq)
		mq->is_optional = 1;
	for (size_t i = 0; i < sizeof HOST_DEVICES / sizeof *HOST_DEVICES; i++) {
		snprintf(source, sizeof source, "%s/%s", in->dev, HOST_DEVICES[i].name);
		snprintf(target, sizeof target, "/dev/%s", HOST_DEVICES[i].name);
		struct mount_step *s = add(p, HOST_DEVICES[i].kind, source, target);
		if (s) {
			s->is_optional = 1;
			s->is_file = HOST_DEVICES[i].is_file;
		}
	}
}

int build_mount_plan(const struct plan_input *in, struct mount_step *steps, int max)
{
	struct plan p = { steps, max, 0 };
	char source[256], target[128];
	add(&p, MOUNT_PROC, "proc", "/proc");
	add(&p, MOUNT_SYSFS, "/sys", "/sys");
	add_dev(&p, in);
	add_tmpfs(&p, "/run", "mode=755", MS_NOSUID | MS_NODEV);
	snprintf(source, sizeof source, "%s/%u", in->wayland_root, in->user_id);
	add(&p, MOUNT_BIND, source, "/run/pc-wayland");
	snprintf(source, sizeof source, "%s/%u", in->media_root, in->user_id);
	snprintf(target, sizeof target, "/home/%s", in->user);
	struct mount_step *home = add(&p, MOUNT_STORAGE, source, target);
	if (home)
		home->is_optional = 1;
	return p.count > max ? -1 : p.count;
}
