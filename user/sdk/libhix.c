// SPDX-License-Identifier: GPL-2.0-only
/* libhix: strings, a small printf and the Kit calls. */
#include <stdarg.h>
#include "hix.h"

void *memcpy(void *d, const void *s, hx_size n)
{
	char *dd = d;
	const char *ss = s;

	while (n--)
		*dd++ = *ss++;
	return d;
}

void *memset(void *d, int c, hx_size n)
{
	char *dd = d;

	while (n--)
		*dd++ = (char)c;
	return d;
}

hx_size hx_strlen(const char *s)
{
	hx_size n = 0;

	while (s[n])
		n++;
	return n;
}

int hx_strcmp(const char *a, const char *b)
{
	while (*a && *a == *b) {
		a++;
		b++;
	}
	return (unsigned char)*a - (unsigned char)*b;
}

long hx_atoi(const char *s)
{
	long v = 0;
	int neg = 0;

	if (*s == '-') {
		neg = 1;
		s++;
	}
	while (*s >= '0' && *s <= '9')
		v = v * 10 + (*s++ - '0');
	return neg ? -v : v;
}

long hx_write_file(const char *path, const void *data, hx_size len)
{
	long fd = hx_open(path, HX_O_WRONLY | HX_O_CREAT | HX_O_TRUNC);
	long n;

	if (fd < 0)
		return fd;
	n = hx_write((int)fd, data, len);
	hx_close((int)fd);
	return n;
}

long hx_read_file(const char *path, char *buf, hx_size size)
{
	long fd = hx_open(path, HX_O_RDONLY);
	long n;

	if (fd < 0)
		return fd;
	n = size ? hx_read((int)fd, buf, size - 1) : 0;
	hx_close((int)fd);
	if (n >= 0 && size)
		buf[n] = '\0';
	return n;
}

void hx_puts(const char *s)
{
	hx_write(1, s, hx_strlen(s));
}

static int put_num(char *out, unsigned long v, unsigned base, int width,
		   char pad, int neg, int left)
{
	char tmp[12];
	int n = 0, len = 0, i;

	do {
		tmp[n++] = "0123456789abcdef"[v % base];
		v /= base;
	} while (v);
	if (neg)
		tmp[n++] = '-';
	if (!left)
		for (i = n; i < width; i++)
			out[len++] = pad;
	while (n)
		out[len++] = tmp[--n];
	if (left)
		for (i = len; i < width; i++)
			out[len++] = ' ';
	return len;
}

int hx_printf(const char *fmt, ...)
{
	char buf[256];
	int len = 0;
	va_list ap;

	va_start(ap, fmt);
	while (*fmt && len < (int)sizeof(buf) - 16) {
		int width = 0, left = 0;
		char pad = ' ';

		if (*fmt != '%') {
			buf[len++] = *fmt++;
			continue;
		}
		fmt++;
		if (*fmt == '-') {
			left = 1;
			fmt++;
		}
		if (*fmt == '0') {
			pad = '0';
			fmt++;
		}
		while (*fmt >= '0' && *fmt <= '9')
			width = width * 10 + (*fmt++ - '0');
		switch (*fmt) {
		case 's': {
			const char *s = va_arg(ap, const char *);
			int n = 0;

			if (!s)
				s = "(null)";
			while (*s && len < (int)sizeof(buf) - 16) {
				buf[len++] = *s++;
				n++;
			}
			if (left)
				while (n++ < width &&
				       len < (int)sizeof(buf) - 16)
					buf[len++] = ' ';
			break;
		}
		case 'c':
			buf[len++] = (char)va_arg(ap, int);
			break;
		case 'd': {
			int v = va_arg(ap, int);

			len += put_num(buf + len, v < 0 ? -(unsigned)v : (unsigned)v,
				       10, width, pad, v < 0, left);
			break;
		}
		case 'u':
			len += put_num(buf + len, va_arg(ap, unsigned), 10, width,
				       pad, 0, left);
			break;
		case 'x':
			len += put_num(buf + len, va_arg(ap, unsigned), 16, width,
				       pad, 0, left);
			break;
		case '%':
			buf[len++] = '%';
			break;
		default:
			buf[len++] = '?';
		}
		if (*fmt)
			fmt++;
	}
	va_end(ap);
	hx_write(1, buf, len);
	return len;
}

/* ---- Kit ------------------------------------------------------------------ */

long kit_lookup(const char *service)
{
	return hx_syscall(SYS_gantry, GANTRY_LOOKUP, (long)service, 0);
}

long kit_call(long handle, unsigned int op, struct kit_msg *m)
{
	return hx_syscall(SYS_gantry, GANTRY_CALL, (long)(handle | (op << 16)),
			  (long)m);
}

static void msg_str(struct kit_msg *m, const char *s)
{
	hx_size n = hx_strlen(s);

	if (n >= KIT_DATA_MAX)
		n = KIT_DATA_MAX - 1;
	memcpy(m->data, s, n);
	m->data[n] = '\0';
	m->len = n + 1;
}

static long call_named(const char *svc, unsigned int op, struct kit_msg *m)
{
	long h = kit_lookup(svc);

	return h < 0 ? h : kit_call(h, op, m);
}

long kit_prop_get(const char *key, char *out, hx_size size)
{
	struct kit_msg m;
	long r;

	msg_str(&m, key);
	r = call_named("kit.prop", KIT_PROP_GET, &m);
	if (r >= 0 && size) {
		hx_size n = hx_strlen(m.data);

		if (n >= size)
			n = size - 1;
		memcpy(out, m.data, n);
		out[n] = '\0';
	}
	return r;
}

long kit_prop_set(const char *key, const char *val)
{
	struct kit_msg m;
	hx_size kn = hx_strlen(key), vn = hx_strlen(val);

	if (kn + vn + 2 > KIT_DATA_MAX)
		return -22;
	memcpy(m.data, key, kn);
	m.data[kn] = '=';
	memcpy(m.data + kn + 1, val, vn + 1);
	m.len = kn + vn + 2;
	return call_named("kit.prop", KIT_PROP_SET, &m);
}

long kit_notify_count(const char *topic)
{
	struct kit_msg m;

	msg_str(&m, topic);
	return call_named("kit.notify", KIT_NOTIFY_COUNT, &m);
}

long kit_notify_post(const char *topic)
{
	struct kit_msg m;

	msg_str(&m, topic);
	return call_named("kit.notify", KIT_NOTIFY_POST, &m);
}
