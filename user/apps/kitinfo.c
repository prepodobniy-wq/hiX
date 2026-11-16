// SPDX-License-Identifier: GPL-2.0-only
/* kitinfo: what a ring 3 program can learn from the Kits. */
#include "../sdk/hix.h"

int main(int argc, char **argv)
{
	(void)argc;
	(void)argv;
	struct kit_msg m;
	char val[64];
	long sys = kit_lookup("kit.system");
	int i;

	hx_printf("kitinfo: pid %d, parent %d, running in ring 3\n",
		  (int)hx_getpid(), (int)hx_getppid());
	if (sys < 0) {
		hx_printf("kit.system not found (%d)\n", (int)sys);
		return 1;
	}
	m.len = 0;
	if (kit_call(sys, KIT_SYS_INFO, &m) > 0)
		hx_printf("system : %s\n", m.data);
	m.len = 0;
	if (kit_call(sys, KIT_SYS_NODES, &m) > 0)
		hx_printf("registry: %s nodes\n", m.data);

	for (i = 0; ; i++) {
		struct kit_msg p;

		p.len = i;
		if (kit_call(kit_lookup("kit.prop"), KIT_PROP_NTH, &p) < 0)
			break;
		hx_printf("prop   : %s\n", p.data);
	}

	if (kit_prop_set("user.last", "kitinfo") < 0)
		hx_puts("prop set failed\n");
	if (kit_prop_get("user.last", val, sizeof(val)) > 0)
		hx_printf("user.last = %s\n", val);
	if (kit_prop_set("hix.release", "x") != -13)	/* -EACCES */
		hx_puts("kernel property was writable!\n");

	kit_notify_post("user.kitinfo");
	hx_printf("events : proc.spawn=%d proc.exit=%d user.kitinfo=%d\n",
		  (int)kit_notify_count("proc.spawn"),
		  (int)kit_notify_count("proc.exit"),
		  (int)kit_notify_count("user.kitinfo"));
	return 0;
}
