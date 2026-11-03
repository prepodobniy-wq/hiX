// SPDX-License-Identifier: GPL-2.0-only
/*
 * Scheduler, wait queues, mutexes, task teardown and blocking port IPC,
 * exercised with real context switches (see stubs32.c).
 */
#include <hix/kernel.h>
#include <aether/kalloc.h>
#include <aether/sched.h>
#include <aether/task.h>
#include <aether/waitq.h>
#include <asm/trap.h>
#include "t32.h"

static char trace[256];
static int tlen;

static void emit(char c)
{
	if (tlen < (int)sizeof(trace) - 1)
		trace[tlen++] = c;
	trace[tlen] = 0;
}

/* run the other threads until they are all blocked or gone */
static void drive(int rounds)
{
	int i;

	for (i = 0; i < rounds; i++)
		thread_yield();
}

static void tick(int n)
{
	while (n--) {
		clock_tick();
		thread_yield();		/* let anything that woke run */
	}
}

/* ---- 1. cooperative round robin ------------------------------------- */

static void rr_thread(void *arg)
{
	char me = (char)(long)arg;
	int i;

	for (i = 0; i < 3; i++) {
		emit(me);
		thread_yield();
	}
}

static void test_round_robin(void)
{
	tlen = 0;
	thread_create(kernel_task, "a", rr_thread, (void *)'A');
	thread_create(kernel_task, "b", rr_thread, (void *)'B');
	thread_create(kernel_task, "c", rr_thread, (void *)'C');
	drive(20);
	CHECK(!strcmp(trace, "ABCABCABC"));
}

/* ---- 2. sleep and timeouts ------------------------------------------ */

static unsigned long woke_at[3];

static void sleeper(void *arg)
{
	long n = (long)arg;

	thread_sleep(n * 10);
	woke_at[n - 1] = jiffies;
	emit('0' + n);
}

static void test_sleep(void)
{
	tlen = 0;
	jiffies = 1000;
	thread_create(kernel_task, "s3", sleeper, (void *)3);
	thread_create(kernel_task, "s1", sleeper, (void *)1);
	thread_create(kernel_task, "s2", sleeper, (void *)2);
	drive(5);
	CHECK(tlen == 0);			/* nobody woke early */
	tick(9);
	CHECK(tlen == 0);
	tick(1);				/* t+10: only the shortest */
	CHECK(!strcmp(trace, "1"));
	tick(10);
	CHECK(!strcmp(trace, "12"));
	tick(10);
	CHECK(!strcmp(trace, "123"));
	CHECK(woke_at[0] == 1010 && woke_at[1] == 1020 && woke_at[2] == 1030);
}

/* ---- 3. wait queue wake-up ------------------------------------------ */

static DECLARE_WAITQ(wq);
static int condition;

static void waiter(void *arg)
{
	unsigned long flags;

	(void)arg;
	local_irq_save(flags);
	while (!condition)
		waitq_wait_irq(&wq, 0);
	local_irq_restore(flags);
	emit('W');
}

static void test_waitq(void)
{
	tlen = 0;
	condition = 0;
	thread_create(kernel_task, "w1", waiter, NULL);
	thread_create(kernel_task, "w2", waiter, NULL);
	drive(5);
	CHECK(tlen == 0);
	waitq_wake_one(&wq);			/* condition still false: sleeps again */
	drive(5);
	CHECK(tlen == 0);
	condition = 1;
	waitq_wake_one(&wq);
	drive(5);
	CHECK(!strcmp(trace, "W"));
	waitq_wake_all(&wq);
	drive(5);
	CHECK(!strcmp(trace, "WW"));
}

/* ---- 4. mutex --------------------------------------------------------- */

static struct mutex mtx = MUTEX_INIT(mtx);
static int counter;

static void mutex_worker(void *arg)
{
	int i;

	(void)arg;
	for (i = 0; i < 5; i++) {
		int v;

		mutex_lock(&mtx);
		v = counter;
		thread_yield();			/* hold the lock across a switch */
		counter = v + 1;
		mutex_unlock(&mtx);
	}
}

static void test_mutex(void)
{
	counter = 0;
	thread_create(kernel_task, "m1", mutex_worker, NULL);
	thread_create(kernel_task, "m2", mutex_worker, NULL);
	thread_create(kernel_task, "m3", mutex_worker, NULL);
	drive(100);
	CHECK(counter == 15);			/* no lost updates */
}

/* ---- 5. preemption by time slice -------------------------------------- */

static volatile int spin_stop;
static unsigned long spun[2];

/* each spinner plays the timer too: every 1000 loops a "tick" happens and
 * the thread yields when the scheduler asks for it, like the IRQ exit path */
static void spinner(void *arg)
{
	int me = (int)(long)arg;

	while (!spin_stop) {
		if (++spun[me] % 1000 == 0)
			clock_tick();
		if (need_resched)
			thread_yield();
		if (spun[0] + spun[1] > 400000)
			spin_stop = 1;
	}
}

static void test_slice(void)
{
	thread_create(kernel_task, "spin1", spinner, (void *)0);
	thread_create(kernel_task, "spin2", spinner, (void *)1);
	drive(10);
	CHECK(spin_stop);
	/* both made real progress: neither starved the other */
	CHECK(spun[0] > 50000 && spun[1] > 50000);
}

/* ---- 6. tasks, exit hooks and reaping --------------------------------- */

static int exit_hook_calls;
static struct task *hooked;

static void on_task_exit(struct task *t)
{
	exit_hook_calls++;
	hooked = t;
}

static void short_lived(void *arg)
{
	(void)arg;
	emit('x');
}

static void test_task_exit(void)
{
	struct task *t = task_create("job");
	int freed_before = stacks_freed;

	tlen = 0;
	t->on_exit = on_task_exit;
	thread_create(t, "j1", short_lived, NULL);
	thread_create(t, "j2", short_lived, NULL);
	CHECK(t->nthreads == 2);
	drive(10);
	CHECK(!strcmp(trace, "xx"));
	CHECK(t->nthreads == 0 && t->terminated);
	CHECK(exit_hook_calls == 1 && hooked == t);	/* once, at the last */
	drive(3);					/* reaper runs lazily */
	CHECK(stacks_freed >= freed_before + 2);
	task_destroy(t);
}

/* ---- 7. port IPC with real blocking ----------------------------------- */

static struct task *srv_task, *cli_task;
static ae_port_name_t srv_port;
static int rpc_done;

static void server(void *arg)
{
	struct ae_msg m;
	ae_ret_t kr;

	(void)arg;
	ae_port_allocate(srv_task, &srv_port);
	bootstrap_register(srv_task, "t.srv", srv_port);
	kr = ae_msg_receive(srv_task, srv_port, &m, false, 0);	/* blocks */
	if (kr == AE_OK) {
		m.data[0] += 1;
		m.header.remote_port = m.header.remote_port;	/* reply port */
		m.header.local_port = AE_PORT_NULL;
		kr = ae_msg_send(srv_task, &m, false, 0);
		emit(kr == AE_OK ? 'S' : 'e');
	}
}

static void client(void *arg)
{
	ae_port_name_t dest, reply;
	struct ae_msg m;
	ae_ret_t kr;

	(void)arg;
	while (bootstrap_look_up(cli_task, "t.srv", &dest))
		thread_yield();
	ae_port_allocate(cli_task, &reply);
	memset(&m, 0, sizeof(m));
	m.data[0] = 'a';
	m.header.size = sizeof(m.header) + 1;
	m.header.remote_port = dest;
	m.header.local_port = reply;
	kr = ae_msg(cli_task, &m, AE_SEND_MSG | AE_RCV_MSG, reply, 0);
	if (kr == AE_OK && m.data[0] == 'b')
		rpc_done = 1;
}

static void test_ipc_blocking(void)
{
	tlen = 0;
	srv_task = task_create("srv");
	cli_task = task_create("cli");
	thread_create(srv_task, "srv", server, NULL);
	drive(3);				/* server is now blocked in receive */
	CHECK(srv_port != 0 && !rpc_done);
	thread_create(cli_task, "cli", client, NULL);
	drive(20);
	CHECK(rpc_done);			/* full request/reply round trip */
	CHECK(!strcmp(trace, "S"));
}

static ae_ret_t late_kr;
static struct task *late_task;

static void late_receiver(void *arg)
{
	struct ae_msg m;

	ae_port_name_t port;

	(void)arg;
	late_kr = AE_FAILURE;			/* "still waiting" marker */
	ae_port_allocate(late_task, &port);
	late_kr = ae_msg_receive(late_task, port, &m, true, 100);
}

static void test_ipc_timeout(void)
{
	/* a task's ports die with its last thread (checked here) ... */
	CHECK(ae_msg_receive(srv_task, srv_port, &(struct ae_msg){ 0 },
			       true, 0) == AE_RCV_INVALID_NAME);

	/* ... and a receive with a timeout gives up after 10 ticks */
	late_task = task_create("late");
	thread_create(late_task, "late", late_receiver, NULL);
	drive(3);
	CHECK(late_kr == AE_FAILURE);		/* still waiting */
	tick(9);
	CHECK(late_kr == AE_FAILURE);
	tick(2);				/* 100 ms = 10 ticks */
	CHECK(late_kr == AE_RCV_TIMED_OUT);
}

int main(void)
{
	static char heap[1 << 20] __attribute__((aligned(16)));

	kalloc_init_region(heap, sizeof(heap));
	task_init();
	sched_init();

	t_print("  [round_robin]\n");
	test_round_robin();
	t_print("  [sleep]\n");
	test_sleep();
	t_print("  [waitq]\n");
	test_waitq();
	t_print("  [mutex]\n");
	test_mutex();
	t_print("  [slice]\n");
	test_slice();
	t_print("  [task_exit]\n");
	test_task_exit();
	t_print("  [ipc_blocking]\n");
	test_ipc_blocking();
	t_print("  [ipc_timeout]\n");
	test_ipc_timeout();

	CHECK(kalloc_check() == 0);
	t_print("  %d checks, %d failed\n", tests_run, tests_failed);
	t_exit(tests_failed ? 1 : 0);
	return 0;
}

void _start(void)
{
	main();
}
