// SPDX-License-Identifier: GPL-2.0-only
#include "hosttest.h"
#include <hix/kernel.h>

static void fmt_case(const char *want, const char *fmt, ...)
{
	char buf[128];
	va_list ap;

	va_start(ap, fmt);
	vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	CHECK_STR(buf, want);
}

int main(void)
{
	char buf[8];
	int n;

	fmt_case("hello 42", "hello %d", 42);
	fmt_case("-17", "%d", -17);
	fmt_case("   42|42   |", "%5d|%-5d|", 42, 42);
	fmt_case("00042", "%05d", 42);
	fmt_case("ff FF 0xff", "%x %X %#x", 255, 255, 255);
	fmt_case("0x0000beef", "%#010x", 0xbeef);
	fmt_case("4294967295", "%u", 0xFFFFFFFFu);
	fmt_case("abc|  abc|abc  |ab", "%s|%5s|%-5s|%.2s", "abc", "abc", "abc",
		 "abc");
	fmt_case("(null)", "%s", (char *)0);
	fmt_case("A%", "%c%%", 'A');
	fmt_case("17", "%o", 15);
	fmt_case("+5 -5", "%+d %+d", 5, -5);
	fmt_case("       7", "%*d", 8, 7);
	fmt_case("-2147483648", "%d", (int)-2147483647 - 1);
	fmt_case("0", "%d", 0);
	fmt_case("", "%.0d", 0);

	/* truncation: snprintf reports the full length, buffer stays valid */
	n = snprintf(buf, sizeof(buf), "%s", "0123456789");
	CHECK(n == 10);
	CHECK_STR(buf, "0123456");

	CHECK(strlen("abc") == 3);
	CHECK(strcmp("a", "b") < 0 && strcmp("b", "a") > 0 && !strcmp("x", "x"));
	CHECK(strncmp("abcd", "abxx", 2) == 0);
	CHECK(strnlen("abcdef", 3) == 3);
	CHECK(strchr("hello", 'l') && *strchr("hello", 'l') == 'l');
	CHECK(strrchr("hello", 'l') == strchr("hello", 'l') + 1);
	CHECK(!strcmp(strstr("hello world", "wor"), "world"));
	CHECK(strstr("hello", "xyz") == NULL);
	CHECK(!strcmp(strstr("abc", ""), "abc"));
	CHECK(strchr("hello", 'z') == NULL);

	{
		char d[8];

		CHECK(strlcpy(d, "toolongstring", sizeof(d)) == 13);
		CHECK_STR(d, "toolong");
		memset(d, 'x', sizeof(d));
		CHECK(d[3] == 'x');
		memcpy(d, "abcdefg", 8);
		memmove(d + 1, d, 6);		/* overlapping, forwards */
		CHECK_STR(d, "aabcdef");
		memmove(d, d + 1, 7);		/* overlapping, backwards */
		CHECK_STR(d, "abcdef");
		CHECK(memcmp("abc", "abd", 3) < 0);
	}
	CHECK(simple_strtoul("1234", NULL, 10) == 1234);
	CHECK(simple_strtoul("0xff", NULL, 0) == 255);
	CHECK(simple_strtoul("ff", NULL, 16) == 255);
	{
		char *end;

		CHECK(simple_strtoul("12ab", &end, 10) == 12 && *end == 'a');
	}
	TEST_DONE();
}
