/* send_signal only reaches a real process: as root, kill(-1) signals every process on the machine,
 * kill(0) the caller's own group and kill(1) the host's init. Signal 0 only checks, so the test is
 * safe even while the guard is missing. */
#include <unistd.h>

#include "check.h"
#include "sysops.h"

static void refuses_pids_that_reach_more_than_one_process(void)
{
	const struct sys_ops *ops = linux_sys_ops();
	CHECK(ops->send_signal(-1, 0) == -1);
	CHECK(ops->send_signal(0, 0) == -1);
	CHECK(ops->send_signal(1, 0) == -1);
}

static void signals_a_real_process(void)
{
	CHECK(linux_sys_ops()->send_signal(getpid(), 0) == 0);
}

int main(void)
{
	RUN(refuses_pids_that_reach_more_than_one_process);
	RUN(signals_a_real_process);
	return CHECK_DONE();
}
