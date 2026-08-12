/* SPDX-License-Identifier: GPL-2.0-only */
/* Read-only AK7755 control-register snapshot for R1 bring-up. */

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;

#define O_RDWR 2
#define I2C_M_RD 0x0001
#define I2C_RDWR 0x0707
#define AK7755_ADDR 0x19
#define FIRST_REG 0xc0
#define LAST_REG 0xea

struct i2c_msg {
	u16 addr;
	u16 flags;
	u16 len;
	u8 *buf;
};

struct i2c_rdwr_ioctl_data {
	struct i2c_msg *msgs;
	u32 nmsgs;
};

void *memcpy(void *dst, const void *src, u32 len)
{
	u8 *out = dst;
	const u8 *in = src;
	u32 i;

	for (i = 0; i < len; i++)
		out[i] = in[i];
	return dst;
}

static long syscall1(long nr, long a0)
{
	register long r0 __asm__("r0") = a0;
	register long r7 __asm__("r7") = nr;
	__asm__ volatile("svc 0" : "+r"(r0) : "r"(r7) : "memory");
	return r0;
}

static long syscall3(long nr, long a0, long a1, long a2)
{
	register long r0 __asm__("r0") = a0;
	register long r1 __asm__("r1") = a1;
	register long r2 __asm__("r2") = a2;
	register long r7 __asm__("r7") = nr;
	__asm__ volatile("svc 0" : "+r"(r0) : "r"(r1), "r"(r2), "r"(r7) : "memory");
	return r0;
}

static u32 str_len(const char *s)
{
	u32 n = 0;

	while (s[n])
		n++;
	return n;
}

static void print(const char *s)
{
	(void)syscall3(4, 1, (long)s, str_len(s));
}

static int read_reg(long fd, u8 reg, u8 *value)
{
	u8 command = reg & 0x7f;
	struct i2c_msg msgs[2];
	struct i2c_rdwr_ioctl_data transfer;
	long ret;

	msgs[0].addr = AK7755_ADDR;
	msgs[0].flags = 0;
	msgs[0].len = 1;
	msgs[0].buf = &command;
	msgs[1].addr = AK7755_ADDR;
	msgs[1].flags = I2C_M_RD;
	msgs[1].len = 1;
	msgs[1].buf = value;
	transfer.msgs = msgs;
	transfer.nmsgs = 2;

	ret = syscall3(54, fd, I2C_RDWR, (long)&transfer);
	return ret == 2 ? 0 : -1;
}

__attribute__((used, noinline)) static int run(void)
{
	static const char hex[] = "0123456789abcdef";
	char line[] = "c0: 00\n";
	u32 reg;
	long fd;
	int failures = 0;

	fd = syscall3(5, (long)"/dev/i2c-1", O_RDWR, 0);
	if (fd < 0) {
		print("AK7755 regdump: open /dev/i2c-1 failed\n");
		return 2;
	}

	for (reg = FIRST_REG; reg <= LAST_REG; reg++) {
		u8 value = 0;

		line[0] = hex[(reg >> 4) & 0xf];
		line[1] = hex[reg & 0xf];
		if (read_reg(fd, (u8)reg, &value)) {
			line[4] = '?';
			line[5] = '?';
			failures++;
		} else {
			line[4] = hex[value >> 4];
			line[5] = hex[value & 0xf];
		}
		print(line);
	}

	(void)syscall1(6, fd);
	return failures ? 3 : 0;
}

__attribute__((naked, noreturn)) void _start(void)
{
	__asm__ volatile(
		"bl run\n"
		"mov r7, #1\n"
		"svc 0\n");
	__builtin_unreachable();
}
