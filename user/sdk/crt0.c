// SPDX-License-Identifier: GPL-2.0-only
/*
 * Program entry. The kernel starts a program in ring 3 with argc, the argv
 * array and the strings on the stack (System V layout); _start hands them to
 * main() and turns its return value into the exit status.
 */
#include "hix.h"

int main(int argc, char **argv);

void _start_c(int argc, char **argv)
{
	hx_exit(main(argc, argv));
}

__asm__(".globl _start\n"
	"_start:\n"
	"	xorl	%ebp, %ebp\n"
	"	movl	(%esp), %eax\n"		/* argc */
	"	leal	4(%esp), %edx\n"	/* argv */
	"	andl	$-16, %esp\n"
	"	subl	$8, %esp\n"
	"	pushl	%edx\n"
	"	pushl	%eax\n"
	"	call	_start_c\n"
	"1:	hlt\n"
	"	jmp	1b\n");
