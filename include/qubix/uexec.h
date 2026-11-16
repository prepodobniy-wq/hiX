/* SPDX-License-Identifier: GPL-2.0-only */
/* qubix: ring 3 demo programs (qprog entry points). */
#ifndef _QUBIX_UEXEC_H
#define _QUBIX_UEXEC_H

#include <hix/types.h>

int uexec_hello(int argc, char **argv);	/* prints a line, exits 0 */
int uexec_fault(int argc, char **argv);	/* reads kernel memory: killed */
int uexec_probe(int argc, char **argv);	/* tries privileged things: killed */

/* programs in /bin (unpacked from the initfs) */
bool uexec_exists(const char *name);
int uexec_file(const char *name, int argc, char **argv);	/* only returns on failure */

#endif /* _QUBIX_UEXEC_H */
