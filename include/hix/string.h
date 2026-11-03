/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _HIX_STRING_H
#define _HIX_STRING_H

#include <hix/types.h>

void *memset(void *s, int c, size_t n);
void *memcpy(void *dst, const void *src, size_t n);
void *memmove(void *dst, const void *src, size_t n);
int memcmp(const void *a, const void *b, size_t n);

size_t strlen(const char *s);
size_t strnlen(const char *s, size_t max);
int strcmp(const char *a, const char *b);
int strncmp(const char *a, const char *b, size_t n);
char *strncpy(char *dst, const char *src, size_t n);
size_t strlcpy(char *dst, const char *src, size_t size);
char *strchr(const char *s, int c);
char *strrchr(const char *s, int c);
char *strstr(const char *haystack, const char *needle);

/* parse an unsigned number; @base 0 means auto-detect (0x.. or decimal) */
unsigned long simple_strtoul(const char *s, char **end, unsigned int base);

#endif /* _HIX_STRING_H */
