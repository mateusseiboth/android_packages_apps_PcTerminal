/* Small file helpers. */
#pragma once
#include <stddef.h>

/* A whole small file (at most cap - 1 bytes) as text; -1 when missing or larger. */
int read_text_file(const char *path, char *out, size_t cap);
/* Writes text to path.tmp, then renames it over path. */
int save_text_file(const char *path, const char *text, unsigned mode);
/* mkdir -p */
int make_dirs(const char *path, unsigned mode);
/* rm -rf below dirfd's name without following links or entering other file systems. */
int remove_tree_at(int dirfd, const char *name);
