/* SPDX-License-Identifier: GPL-2.0-only */
/* Minimal freestanding AK7755EN device-identification reader. */

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;

#define O_RDWR 2
#define I2C_M_RD 0x0001
#define I2C_RDWR 0x0707

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
	syscall3(4, 1, (long)s, str_len(s));
}

__attribute__((used, noinline)) static int run(void)
{
	u8 command = 0x60;
	u8 id = 0;
	char line[] = "AK7755EN device_id=0x00 expected=0x55\n";
	static const char hex[] = "0123456789abcdef";
	struct i2c_msg msgs[2];
	struct i2c_rdwr_ioctl_data transfer;
	long fd;
	long ret;

	fd = syscall3(5, (long)"/dev/i2c-1", O_RDWR, 0);
	if (fd < 0) {
		print("AK7755EN: open /dev/i2c-1 failed\n");
		return 2;
	}

	msgs[0].addr = 0x19;
	msgs[0].flags = 0;
	msgs[0].len = 1;
	msgs[0].buf = &command;
	msgs[1].addr = 0x19;
	msgs[1].flags = I2C_M_RD;
	msgs[1].len = 1;
	msgs[1].buf = &id;
	transfer.msgs = msgs;
	transfer.nmsgs = 2;

	ret = syscall3(54, fd, I2C_RDWR, (long)&transfer);
	syscall1(6, fd);
	if (ret != 2) {
		print("AK7755EN: combined 0x60 identification read failed\n");
		return 3;
	}

	line[21] = hex[id >> 4];
	line[22] = hex[id & 0xf];
	print(line);
	return id == 0x55 ? 0 : 4;
}

__attribute__((naked, noreturn)) void _start(void)
{
	__asm__ volatile(
		"bl run\n"
		"mov r7, #1\n"
		"svc 0\n");
	__builtin_unreachable();
}
