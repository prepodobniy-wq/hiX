// SPDX-License-Identifier: GPL-2.0-only
/* primes: a sieve with a static array: exercises .bss in ring 3. */
#include "../sdk/hix.h"

#define N 2000
static unsigned char composite[N];

int main(int argc, char **argv)
{
	(void)argc;
	(void)argv;
	int i, j, n = 0;

	for (i = 2; i < N; i++) {
		if (composite[i])
			continue;
		n++;
		for (j = i * 2; j < N; j += i)
			composite[j] = 1;
	}
	hx_printf("primes below %d: %d (last %d)\n", N, n, 1999);
	return n == 303 ? 0 : 1;
}
