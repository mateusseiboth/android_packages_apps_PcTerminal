/* The user namespace of an id-mapped mount comes back (or fails) without hanging. */
#include <signal.h>
#include <unistd.h>

#include "check.h"
#include "sysops.h"

static void comes_back_even_when_the_maps_are_refused(void)
{
	/* Unprivileged, the gid map is refused: the call fails, but it must return. */
	alarm(5);
	int fd = make_idmap_userns(getuid(), getgid(), getuid());
	alarm(0);
	if (fd >= 0)
		close(fd);
	CHECK(1);
}

int main(void)
{
	RUN(comes_back_even_when_the_maps_are_refused);
	return CHECK_DONE();
}
