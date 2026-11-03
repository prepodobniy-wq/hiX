// SPDX-License-Identifier: GPL-2.0-only
/*
 * qubix: built-in programs. Each one is a normal process (own task, own
 * pid, own file descriptors) that talks to the kernel only through libq.
 * `sh` lives in shell.c.
 */
#include <hix/kernel.h>
#include <qubix/libq.h>
#include <qubix/proc.h>

int sh_main(int argc, char **argv);
int desk_main(int argc, char **argv);

/* pid 1: starts the shell and starts it again whenever it exits */
static int init_main(int argc, char **argv)
{
	int status;
	long pid;

	(void)argc;
	(void)argv;
	for (;;) {
		pid = q_spawn("sh");
		if (pid < 0) {
			q_printf("init: cannot start sh (%ld)\n", pid);
			q_sleep_ms(1000);
			continue;
		}
		q_wait4(pid, &status);
		q_printf("init: sh (pid %ld) exited with %d, restarting\n",
			 pid, status);
	}
	return 0;
}

static int hello_main(int argc, char **argv)
{
	int i;

	q_printf("hello from pid %ld (parent %ld), %d argument(s)\n",
		 q_getpid(), q_getppid(), argc - 1);
	for (i = 1; i < argc; i++)
		q_printf("  argv[%d] = %s\n", i, argv[i]);
	return 0;
}

/* burn CPU for N seconds: shows preemption and the CPU column of ps */
static int spin_main(int argc, char **argv)
{
	unsigned long secs = argc > 1 ? simple_strtoul(argv[1], NULL, 10) : 3;
	long start = q_time();
	volatile unsigned long x = 0;

	if (secs > 60)
		secs = 60;
	while (q_time() - start < (long)secs)
		x++;
	q_printf("spin %ld: done after %lu s\n", q_getpid(), secs);
	return 0;
}

/*
 * A port IPC demonstration in two processes. The server registers
 * "hix.upper" with the bootstrap service and upper-cases whatever it
 * receives, replying on the reply port that came with the request.
 */
static int ipcsrv_main(int argc, char **argv)
{
	ae_port_name_t port = q_ae_reply_port();	/* a fresh receive right */
	struct ae_msg msg;
	ae_ret_t kr;
	int served = 0;

	(void)argc;
	(void)argv;
	kr = q_bootstrap_register("hix.upper", port);
	if (kr) {
		q_printf("ipcsrv: register failed: %d\n", kr);
		return 1;
	}
	q_printf("ipcsrv: listening on port %#x as hix.upper\n", port);

	while (served < 3) {
		u32 len, i;
		ae_port_name_t reply;

		kr = q_ae_msg(&msg, AE_RCV_MSG | AE_RCV_TIMEOUT, port,
				5000);
		if (kr) {
			q_printf("ipcsrv: receive: %#x, giving up\n", kr);
			break;
		}
		reply = msg.header.remote_port;
		len = msg.header.size - sizeof(msg.header);
		for (i = 0; i < len; i++)
			if (msg.data[i] >= 'a' && msg.data[i] <= 'z')
				msg.data[i] -= 32;
		q_printf("ipcsrv: request %u (%u bytes) -> reply port %#x\n",
			 msg.header.id, len, reply);
		msg.header.remote_port = reply;
		msg.header.local_port = AE_PORT_NULL;
		q_ae_msg(&msg, AE_SEND_MSG, 0, 0);
		served++;
	}
	q_port_deallocate(port);
	return 0;
}

static int ipccli_main(int argc, char **argv)
{
	static const char * const words[] = { "hello", "aether", "ports" };
	ae_port_name_t server, reply;
	struct ae_msg msg;
	ae_ret_t kr;
	int i, tries = 0;

	(void)argc;
	(void)argv;
	while ((kr = q_bootstrap_look_up("hix.upper", &server)) != 0) {
		if (++tries > 20) {
			q_printf("ipccli: no such service (%d)\n", kr);
			return 1;
		}
		q_sleep_ms(50);
	}
	reply = q_ae_reply_port();
	q_printf("ipccli: server port %#x, my reply port %#x\n", server,
		 reply);

	for (i = 0; i < 3; i++) {
		size_t n = strlen(words[i]) + 1;

		memset(&msg, 0, sizeof(msg));
		memcpy(msg.data, words[i], n);
		msg.header.size = sizeof(msg.header) + n;
		msg.header.remote_port = server;
		msg.header.local_port = reply;
		msg.header.id = i + 1;
		kr = q_ae_msg(&msg, AE_SEND_MSG | AE_RCV_MSG |
				AE_RCV_TIMEOUT, reply, 2000);
		if (kr) {
			q_printf("ipccli: rpc failed: %#x\n", kr);
			return 1;
		}
		q_printf("ipccli: '%s' -> '%s'\n", words[i], (char *)msg.data);
	}
	return 0;
}

const struct qprog qprogs[] = {
	{ "init", "the init process (pid 1)", init_main },
	{ "sh", "the shell", sh_main },
	{ "hello", "print arguments and pids", hello_main },
	{ "spin", "spin [seconds]: busy loop", spin_main },
	{ "ipcsrv", "port IPC demo server", ipcsrv_main },
	{ "ipccli", "port IPC demo client", ipccli_main },
	{ "desk", "desk [program]: the desktop (windows, games)", desk_main },
};

const int qprogs_count = ARRAY_SIZE(qprogs);
