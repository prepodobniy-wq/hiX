// SPDX-License-Identifier: GPL-2.0-only
/*
 * A small printf engine for the kernel.
 *
 * Supported: %c %s %d %i %u %x %X %o %p %%, flags "-+ 0#", field width and
 * precision (also as '*'), and the length modifiers h, l, z, t (all of them
 * are 32 bit on i386). No 64-bit, no floating point.
 */
#include <stdarg.h>
#include <hix/types.h>
#include <hix/string.h>
#include <hix/compiler.h>
#include <hix/printk.h>

#define F_LEFT	BIT(0)
#define F_PLUS	BIT(1)
#define F_SPACE	BIT(2)
#define F_ZERO	BIT(3)
#define F_ALT	BIT(4)
#define F_UPPER	BIT(5)

struct out {
	char *buf;
	size_t size;
	size_t pos;		/* what the output would have been */
};

static void put(struct out *o, char c)
{
	if (o->pos + 1 < o->size)
		o->buf[o->pos] = c;
	o->pos++;
}

static void pad(struct out *o, char c, int n)
{
	while (n-- > 0)
		put(o, c);
}

static void put_number(struct out *o, unsigned long val, bool neg,
		       unsigned int base, unsigned int flags,
		       int width, int prec)
{
	const char *digits = (flags & F_UPPER) ? "0123456789ABCDEF"
					       : "0123456789abcdef";
	char tmp[24];
	char sign = 0;
	const char *prefix = "";
	int ndigits = 0, len, zeros, i;

	if (neg)
		sign = '-';
	else if (flags & F_PLUS)
		sign = '+';
	else if (flags & F_SPACE)
		sign = ' ';

	if (val == 0 && prec == 0) {
		ndigits = 0;		/* "%.0d" of 0 prints nothing */
	} else if (val == 0) {
		tmp[ndigits++] = '0';
	} else {
		while (val) {
			tmp[ndigits++] = digits[val % base];
			val /= base;
		}
	}

	if ((flags & F_ALT) && base == 16 && ndigits)
		prefix = (flags & F_UPPER) ? "0X" : "0x";
	else if ((flags & F_ALT) && base == 8)
		prefix = "0";

	zeros = prec > ndigits ? prec - ndigits : 0;
	len = ndigits + zeros + (sign ? 1 : 0) + (int)strlen(prefix);

	if ((flags & F_ZERO) && !(flags & F_LEFT) && prec < 0 && width > len) {
		zeros += width - len;
		len = width;
	}

	if (!(flags & F_LEFT))
		pad(o, ' ', width - len);
	if (sign)
		put(o, sign);
	while (*prefix)
		put(o, *prefix++);
	pad(o, '0', zeros);
	for (i = ndigits - 1; i >= 0; i--)
		put(o, tmp[i]);
	if (flags & F_LEFT)
		pad(o, ' ', width - len);
}

int vsnprintf(char *buf, size_t size, const char *fmt, va_list ap)
{
	struct out o = { buf, size, 0 };

	while (*fmt) {
		unsigned int flags = 0;
		int width = -1, prec = -1;
		unsigned long val;
		bool is_long = false;

		if (*fmt != '%') {
			put(&o, *fmt++);
			continue;
		}
		fmt++;

		for (;; fmt++) {
			if (*fmt == '-')
				flags |= F_LEFT;
			else if (*fmt == '+')
				flags |= F_PLUS;
			else if (*fmt == ' ')
				flags |= F_SPACE;
			else if (*fmt == '0')
				flags |= F_ZERO;
			else if (*fmt == '#')
				flags |= F_ALT;
			else
				break;
		}

		if (*fmt == '*') {
			width = va_arg(ap, int);
			if (width < 0) {
				flags |= F_LEFT;
				width = -width;
			}
			fmt++;
		} else if (*fmt >= '0' && *fmt <= '9') {
			width = 0;
			while (*fmt >= '0' && *fmt <= '9')
				width = width * 10 + (*fmt++ - '0');
		}

		if (*fmt == '.') {
			fmt++;
			prec = 0;
			if (*fmt == '*') {
				prec = va_arg(ap, int);
				if (prec < 0)
					prec = -1;
				fmt++;
			} else {
				while (*fmt >= '0' && *fmt <= '9')
					prec = prec * 10 + (*fmt++ - '0');
			}
		}

		while (*fmt == 'l' || *fmt == 'h' || *fmt == 'z' || *fmt == 't') {
			if (*fmt == 'l' || *fmt == 'z' || *fmt == 't')
				is_long = true;
			fmt++;
		}

		switch (*fmt) {
		case 'c':
			if (!(flags & F_LEFT))
				pad(&o, ' ', width - 1);
			put(&o, (char)va_arg(ap, int));
			if (flags & F_LEFT)
				pad(&o, ' ', width - 1);
			break;
		case 's': {
			const char *s = va_arg(ap, const char *);
			int len, i;

			if (!s)
				s = "(null)";
			len = prec >= 0 ? (int)strnlen(s, prec)
					: (int)strlen(s);
			if (!(flags & F_LEFT))
				pad(&o, ' ', width - len);
			for (i = 0; i < len; i++)
				put(&o, s[i]);
			if (flags & F_LEFT)
				pad(&o, ' ', width - len);
			break;
		}
		case 'd':
		case 'i': {
			long sv = is_long ? va_arg(ap, long)
					  : (long)va_arg(ap, int);

			put_number(&o, sv < 0 ? -(unsigned long)sv : (unsigned long)sv,
				   sv < 0, 10, flags, width, prec);
			break;
		}
		case 'u':
		case 'x':
		case 'X':
		case 'o':
			val = is_long ? va_arg(ap, unsigned long)
				      : va_arg(ap, unsigned int);
			if (*fmt == 'X')
				flags |= F_UPPER;
			put_number(&o, val, false,
				   *fmt == 'u' ? 10 : *fmt == 'o' ? 8 : 16,
				   flags, width, prec);
			break;
		case 'p':
			val = (unsigned long)(uintptr_t)va_arg(ap, void *);
			put_number(&o, val, false, 16, flags | F_ALT, width,
				   prec);
			break;
		case '%':
			put(&o, '%');
			break;
		case '\0':
			goto done;
		default:
			put(&o, '%');
			put(&o, *fmt);
			break;
		}
		fmt++;
	}
done:
	if (size) {
		if (o.pos < size)
			buf[o.pos] = '\0';
		else
			buf[size - 1] = '\0';
	}
	return (int)o.pos;
}

int snprintf(char *buf, size_t size, const char *fmt, ...)
{
	va_list ap;
	int n;

	va_start(ap, fmt);
	n = vsnprintf(buf, size, fmt, ap);
	va_end(ap);
	return n;
}
