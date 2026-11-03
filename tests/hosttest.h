/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef HOSTTEST_H
#define HOSTTEST_H

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>

extern int tests_run, tests_failed;

#define CHECK(cond) do {						\
	tests_run++;							\
	if (!(cond)) {							\
		tests_failed++;						\
		printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);	\
	}								\
} while (0)

#define CHECK_STR(got, want) do {					\
	tests_run++;							\
	if (strcmp((got), (want))) {					\
		tests_failed++;						\
		printf("  FAIL %s:%d: \"%s\" != \"%s\"\n", __FILE__,	\
		       __LINE__, (got), (want));			\
	}								\
} while (0)

#define TEST_DONE() do {						\
	printf("  %d checks, %d failed\n", tests_run, tests_failed);	\
	return tests_failed ? 1 : 0;					\
} while (0)

#endif
