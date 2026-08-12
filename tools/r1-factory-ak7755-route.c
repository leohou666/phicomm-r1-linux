// SPDX-License-Identifier: GPL-2.0-only
/* Apply the exact R1 Android HAL media-speaker controls to factory Linux. */

typedef unsigned char u8;
typedef unsigned int u32;
typedef long s32;

#define __naked __attribute__((naked))
#define __noreturn __attribute__((noreturn))

#define O_RDWR 2
#define SNDRV_CTL_ELEM_IFACE_MIXER 2
#define SNDRV_CTL_ELEM_TYPE_INTEGER 2
#define SNDRV_CTL_ELEM_TYPE_ENUMERATED 3

#define IOC_READ 2U
#define IOC_WRITE 1U
#define IOC(dir, type, nr, size) \
	(((dir) << 30) | ((size) << 16) | ((type) << 8) | (nr))

struct snd_ctl_elem_id {
	u32 numid;
	u32 iface;
	u32 device;
	u32 subdevice;
	u8 name[44];
	u32 index;
};

struct snd_ctl_elem_info {
	struct snd_ctl_elem_id id;
	u32 type;
	u32 access;
	u32 count;
	s32 owner;
	union {
		struct {
			s32 min;
			s32 max;
			s32 step;
		} integer;
		struct {
			u32 items;
			u32 item;
			char name[64];
			unsigned long long names_ptr;
			u32 names_length;
		} enumerated;
		u8 reserved[128];
	} value;
	u8 reserved[64];
};

struct snd_ctl_elem_value {
	struct snd_ctl_elem_id id;
	u32 indirect;
	union {
		s32 integer[128];
		unsigned long long integer64[64];
		u32 enumerated[128];
		u8 bytes[512];
	} value;
	u8 reserved[128];
};

#define SNDRV_CTL_IOCTL_ELEM_INFO \
	IOC(IOC_READ | IOC_WRITE, 'U', 0x11, sizeof(struct snd_ctl_elem_info))
#define SNDRV_CTL_IOCTL_ELEM_WRITE \
	IOC(IOC_READ | IOC_WRITE, 'U', 0x13, sizeof(struct snd_ctl_elem_value))

_Static_assert(sizeof(struct snd_ctl_elem_id) == 64,
	       "ARM control element ID ABI mismatch");
_Static_assert(sizeof(struct snd_ctl_elem_info) == 272,
	       "ARM control element info ABI mismatch");
_Static_assert(sizeof(struct snd_ctl_elem_value) == 712,
	       "ARM control element value ABI mismatch");

struct route_control {
	const char *name;
	const char *enum_name;
	s32 value0;
	s32 value1;
};

/* Recovered from audio.primary.rk30board.so ak7755_speaker_normal_controls. */
static const struct route_control media_speaker[] = {
	{ "DRAM Size(Bank1:Bank0)", 0, 1, 1 },
	{ "DLRAM Mode(Bank1:Bank0)", 0, 2, 2 },
	{ "POMODE DLRAM Pointer 0", 0, 1, 1 },
	{ "DSP Firmware PRAM", "data2", 0, 0 },
	{ "DSP Firmware CRAM", "data2", 0, 0 },
	{ "DSP Firmware OFREG", "data2", 0, 0 },
	{ "DAC MUX", "DSP", 0, 0 },
	{ "Line Out Volume 1", 0, 15, 15 },
	{ "Line Out Volume 2", 0, 15, 15 },
	{ "LineOut Amp1", "On", 0, 0 },
	{ "LineOut Amp2", "On", 0, 0 },
};

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

	__asm__ volatile("svc 0" : "+r"(r0)
			 : "r"(r1), "r"(r2), "r"(r7) : "memory");
	return r0;
}

void *memset(void *dst, int value, u32 len)
{
	u8 *out = dst;
	u32 i;

	for (i = 0; i < len; i++)
		out[i] = (u8)value;
	return dst;
}

static u32 str_len(const char *s)
{
	u32 len = 0;

	while (s[len])
		len++;
	return len;
}

static int str_equal(const char *a, const char *b)
{
	u32 i = 0;

	while (a[i] && b[i]) {
		if (a[i] != b[i])
			return 0;
		i++;
	}
	return a[i] == b[i];
}

static void copy_name(u8 *dst, const char *src)
{
	u32 i;

	for (i = 0; i < 43 && src[i]; i++)
		dst[i] = (u8)src[i];
	dst[i] = 0;
}

static void putstr(const char *s)
{
	(void)syscall3(4, 1, (long)s, str_len(s));
}

static void put_u32(u32 value)
{
	char out[10];
	u32 divisor = 1000000000U;
	u32 len = 0;
	int started = 0;

	while (divisor) {
		u8 digit = 0;

		while (value >= divisor) {
			value -= divisor;
			digit++;
		}
		if (digit || started || divisor == 1) {
			out[len++] = (char)('0' + digit);
			started = 1;
		}
		divisor /= 10;
	}
	(void)syscall3(4, 1, (long)out, len);
}

static void put_error(const char *operation, const char *name, long error)
{
	putstr("FAIL: ");
	putstr(operation);
	putstr(" control=\"");
	putstr(name);
	putstr("\" errno=");
	put_u32((u32)-error);
	putstr("\n");
}

static void init_id(struct snd_ctl_elem_id *id, const char *name)
{
	memset(id, 0, sizeof(*id));
	id->iface = SNDRV_CTL_ELEM_IFACE_MIXER;
	copy_name(id->name, name);
}

static int find_enum_item(long fd, struct snd_ctl_elem_info *info,
			  const char *wanted, u32 *result)
{
	u32 item;
	long ret;

	for (item = 0; item < info->value.enumerated.items; item++) {
		info->value.enumerated.item = item;
		ret = syscall3(54, fd, SNDRV_CTL_IOCTL_ELEM_INFO, (long)info);
		if (ret < 0)
			return (int)ret;
		if (str_equal(info->value.enumerated.name, wanted)) {
			*result = item;
			return 0;
		}
	}
	return -22;
}

static int apply_control(long fd, const struct route_control *control)
{
	struct snd_ctl_elem_info info;
	struct snd_ctl_elem_value value;
	u32 item = 0;
	long ret;

	memset(&info, 0, sizeof(info));
	init_id(&info.id, control->name);
	ret = syscall3(54, fd, SNDRV_CTL_IOCTL_ELEM_INFO, (long)&info);
	if (ret < 0) {
		put_error("info", control->name, ret);
		return (int)ret;
	}

	memset(&value, 0, sizeof(value));
	value.id = info.id;
	if (control->enum_name) {
		if (info.type != SNDRV_CTL_ELEM_TYPE_ENUMERATED) {
			putstr("FAIL: expected enumerated control=\"");
			putstr(control->name);
			putstr("\"\n");
			return -22;
		}
		ret = find_enum_item(fd, &info, control->enum_name, &item);
		if (ret < 0) {
			put_error("enum lookup", control->name, ret);
			return (int)ret;
		}
		value.value.enumerated[0] = item;
	} else if (info.type == SNDRV_CTL_ELEM_TYPE_ENUMERATED) {
		/* The factory tinymix script sets these enum controls by index. */
		if ((u32)control->value0 >= info.value.enumerated.items) {
			putstr("FAIL: enum index out of range control=\"");
			putstr(control->name);
			putstr("\"\n");
			return -22;
		}
		value.value.enumerated[0] = (u32)control->value0;
	} else if (info.type == SNDRV_CTL_ELEM_TYPE_INTEGER) {
		value.value.integer[0] = control->value0;
		value.value.integer[1] = control->value1;
	} else {
		putstr("FAIL: unsupported control type=\"");
		putstr(control->name);
		putstr("\"\n");
		return -22;
	}

	ret = syscall3(54, fd, SNDRV_CTL_IOCTL_ELEM_WRITE, (long)&value);
	if (ret < 0) {
		put_error("write", control->name, ret);
		return (int)ret;
	}
	putstr("applied=\"");
	putstr(control->name);
	putstr("\"");
	if (control->enum_name) {
		putstr(" value=\"");
		putstr(control->enum_name);
		putstr("\"");
	}
	putstr("\n");
	return 0;
}

static int __attribute__((used)) main(void)
{
	long fd;
	u32 i;
	int ret;

	putstr("R1 factory AK7755 media-speaker route: applying 11 recovered HAL controls.\n");
	fd = syscall3(5, (long)"/dev/snd/controlC0", O_RDWR, 0);
	if (fd < 0) {
		put_error("open", "/dev/snd/controlC0", fd);
		return 1;
	}
	for (i = 0; i < sizeof(media_speaker) / sizeof(media_speaker[0]); i++) {
		ret = apply_control(fd, &media_speaker[i]);
		if (ret < 0) {
			(void)syscall1(6, fd);
			return 1;
		}
	}
	(void)syscall1(6, fd);
	putstr("FACTORY_AK7755_ROUTE_APPLIED controls=11 profile=media-speaker data=data2\n");
	return 0;
}

__attribute__((naked, noreturn)) void _start(void)
{
	__asm__ volatile(
		"bl main\n"
		"mov r7, #1\n"
		"svc 0\n"
		"b .\n");
}
