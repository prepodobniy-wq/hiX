/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef T32_H
#define T32_H
void t_exit(int code);
void t_print(const char *fmt, ...);
void t_check(int ok, const char *expr, int line);
extern int tests_run, tests_failed, stacks_freed;
#define CHECK(c) t_check(!!(c), #c, __LINE__)
#endif
