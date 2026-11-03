// SPDX-License-Identifier: GPL-2.0-only
/*
 * Freestanding string and memory helpers.
 *
 * Built with -fno-tree-loop-distribute-patterns: otherwise GCC may turn the
 * loops below back into calls to memset()/memcpy() themselves.
 */
#include <hix/string.h>

void *memset(void *s, int c, size_t n)
{
	unsigned char *p = s;

	while (n--)
		*p++ = (unsigned char)c;
	return s;
}

void *memcpy(void *dst, const void *src, size_t n)
{
	unsigned char *d = dst;
	const unsigned char *s = src;

	while (n--)
		*d++ = *s++;
	return dst;
}

void *memmove(void *dst, const void *src, size_t n)
{
	unsigned char *d = dst;
	const unsigned char *s = src;

	if (d == s || n == 0)
		return dst;
	if (d < s) {
		while (n--)
			*d++ = *s++;
	} else {
		d += n;
		s += n;
		while (n--)
			*--d = *--s;
	}
	return dst;
}

int memcmp(const void *a, const void *b, size_t n)
{
	const unsigned char *p = a, *q = b;

	while (n--) {
		if (*p != *q)
			return *p - *q;
		p++;
		q++;
	}
	return 0;
}

size_t strlen(const char *s)
{
	size_t n = 0;

	while (s[n])
		n++;
	return n;
}

size_t strnlen(const char *s, size_t max)
{
	size_t n = 0;

	while (n < max && s[n])
		n++;
	return n;
}

int strcmp(const char *a, const char *b)
{
	while (*a && *a == *b) {
		a++;
		b++;
	}
	return (unsigned char)*a - (unsigned char)*b;
}

int strncmp(const char *a, const char *b, size_t n)
{
	while (n && *a && *a == *b) {
		a++;
		b++;
		n--;
	}
	if (!n)
		return 0;
	return (unsigned char)*a - (unsigned char)*b;
}

char *strncpy(char *dst, const char *src, size_t n)
{
	size_t i;

	for (i = 0; i < n && src[i]; i++)
		dst[i] = src[i];
	for (; i < n; i++)
		dst[i] = '\0';
	return dst;
}

/* copies at most size-1 bytes and always terminates; returns strlen(src) */
size_t strlcpy(char *dst, const char *src, size_t size)
{
	size_t len = strlen(src);

	if (size) {
		size_t n = len >= size ? size - 1 : len;

		memcpy(dst, src, n);
		dst[n] = '\0';
	}
	return len;
}

char *strchr(const char *s, int c)
{
	for (; *s; s++)
		if (*s == (char)c)
			return (char *)s;
	return c == 0 ? (char *)s : NULL;
}

char *strstr(const char *haystack, const char *needle)
{
	size_t n = strlen(needle);

	if (!n)
		return (char *)haystack;
	for (; *haystack; haystack++)
		if (!strncmp(haystack, needle, n))
			return (char *)haystack;
	return NULL;
}

char *strrchr(const char *s, int c)
{
	const char *last = NULL;

	for (; *s; s++)
		if (*s == (char)c)
			last = s;
	if (c == 0)
		return (char *)s;
	return (char *)last;
}

static int digit_value(char c)
{
	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	if (c >= 'A' && c <= 'F')
		return c - 'A' + 10;
	return 99;
}

unsigned long simple_strtoul(const char *s, char **end, unsigned int base)
{
	unsigned long val = 0;

	if ((base == 0 || base == 16) && s[0] == '0' &&
	    (s[1] == 'x' || s[1] == 'X')) {
		s += 2;
		base = 16;
	} else if (base == 0) {
		base = 10;
	}

	while (digit_value(*s) < (int)base) {
		val = val * base + digit_value(*s);
		s++;
	}
	if (end)
		*end = (char *)s;
	return val;
}
