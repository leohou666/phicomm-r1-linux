// SPDX-License-Identifier: GPL-2.0-only
/* Freestanding ALSA PCM capture statistics test for the Phicomm R1. */

typedef signed short s16;
typedef unsigned char u8;
typedef unsigned int u32;

#define __used __attribute__((used))
#define __noinline __attribute__((noinline))
#define __naked __attribute__((naked))
#define __noreturn __attribute__((noreturn))

#define O_RDONLY 0
#define EPIPE 32

#define SNDRV_PCM_ACCESS_RW_INTERLEAVED 3
#define SNDRV_PCM_FORMAT_S16_LE 2
#define SNDRV_PCM_SUBFORMAT_STD 0
#define SNDRV_PCM_HW_PARAM_ACCESS 0
#define SNDRV_PCM_HW_PARAM_FORMAT 1
#define SNDRV_PCM_HW_PARAM_SUBFORMAT 2
#define SNDRV_PCM_HW_PARAM_SAMPLE_BITS 8
#define SNDRV_PCM_HW_PARAM_CHANNELS 10
#define SNDRV_PCM_HW_PARAM_RATE 11
#define SNDRV_PCM_HW_PARAM_PERIOD_SIZE 13
#define SNDRV_PCM_HW_PARAM_PERIODS 15
#define SNDRV_PCM_HW_PARAM_BUFFER_SIZE 17
#define SNDRV_PCM_HW_PARAM_LAST_INTERVAL 19
#define SNDRV_PCM_IOCTL_HW_REFINE 0xc25c4110U
#define SNDRV_PCM_IOCTL_HW_PARAMS 0xc25c4111U
#define SNDRV_PCM_IOCTL_PREPARE 0x00004140U
#define SNDRV_PCM_IOCTL_DROP 0x00004143U

#define PCM_RATE 48000U
#define PCM_CHANNELS 2U
#define PCM_FRAME_BYTES 4U
#define PCM_PERIOD_FRAMES 1024U
#define PCM_PERIOD_BYTES (PCM_PERIOD_FRAMES * PCM_FRAME_BYTES)
#define PCM_PERIODS 4U
#define PCM_BUFFER_FRAMES (PCM_PERIOD_FRAMES * PCM_PERIODS)
#define DEFAULT_SECONDS 60U
#define MAX_SECONDS 300U

struct snd_mask {
	u32 bits[8];
};

struct snd_interval {
	u32 min;
	u32 max;
	u32 openmin:1;
	u32 openmax:1;
	u32 integer:1;
	u32 empty:1;
};

struct snd_pcm_hw_params {
	u32 flags;
	struct snd_mask masks[3];
	struct snd_mask mres[5];
	struct snd_interval intervals[SNDRV_PCM_HW_PARAM_LAST_INTERVAL -
				      SNDRV_PCM_HW_PARAM_SAMPLE_BITS + 1];
	struct snd_interval ires[9];
	u32 rmask;
	u32 cmask;
	u32 info;
	u32 msbits;
	u32 rate_num;
	u32 rate_den;
	u32 fifo_size;
	u8 sync[16];
	u8 reserved[48];
};

_Static_assert(sizeof(struct snd_pcm_hw_params) == 604,
	       "ARM snd_pcm_hw_params ABI size mismatch");

static s16 capture_period[PCM_PERIOD_FRAMES * PCM_CHANNELS];

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

static void putstr(const char *s)
{
	(void)syscall3(4, 1, (long)s, str_len(s));
}

static void put_u32(u32 value)
{
	static const u32 powers[] = {
		1000000000U, 100000000U, 10000000U, 1000000U, 100000U,
		10000U, 1000U, 100U, 10U, 1U
	};
	char out[10];
	u32 i;
	u32 len = 0;
	int started = 0;

	for (i = 0; i < 10; i++) {
		u8 digit = 0;

		while (value >= powers[i]) {
			value -= powers[i];
			digit++;
		}
		if (digit || started || i == 9) {
			out[len++] = (char)('0' + digit);
			started = 1;
		}
	}
	(void)syscall3(4, 1, (long)out, len);
}

static void put_error(const char *operation, long error)
{
	putstr(operation);
	putstr(" failed errno=");
	put_u32((u32)-error);
	putstr("\n");
}

static u32 parse_seconds(const char *value)
{
	u32 seconds = 0;
	u32 i = 0;

	if (!value || !value[0])
		return 0;
	while (value[i]) {
		if (value[i] < '0' || value[i] > '9')
			return 0;
		seconds = seconds * 10U + (u32)(value[i] - '0');
		if (seconds > MAX_SECONDS)
			return 0;
		i++;
	}
	return seconds;
}

static u32 divide_u32(u32 dividend, u32 divisor)
{
	u32 quotient = 0;
	u32 remainder = 0;
	int bit;

	for (bit = 31; bit >= 0; bit--) {
		remainder = (remainder << 1) | ((dividend >> bit) & 1U);
		if (remainder >= divisor) {
			remainder -= divisor;
			quotient |= 1U << bit;
		}
	}
	return quotient;
}

static u32 integer_sqrt(u32 value)
{
	u32 root = 0;
	u32 bit = 1U << 30;

	while (bit > value)
		bit >>= 2;
	while (bit) {
		if (value >= root + bit) {
			value -= root + bit;
			root = (root >> 1) + bit;
		} else {
			root >>= 1;
		}
		bit >>= 2;
	}
	return root;
}

static struct snd_interval *param_interval(struct snd_pcm_hw_params *params,
					   u32 parameter)
{
	return &params->intervals[parameter - SNDRV_PCM_HW_PARAM_SAMPLE_BITS];
}

static void params_any(struct snd_pcm_hw_params *params)
{
	u32 i;
	u32 j;

	memset(params, 0, sizeof(*params));
	for (i = 0; i < 3; i++)
		for (j = 0; j < 8; j++)
			params->masks[i].bits[j] = ~0U;
	for (i = 0; i < 12; i++) {
		params->intervals[i].min = 0;
		params->intervals[i].max = ~0U;
	}
	params->rmask = (1U << (SNDRV_PCM_HW_PARAM_LAST_INTERVAL + 1)) - 1U;
}

static void set_mask(struct snd_pcm_hw_params *params, u32 parameter, u32 value)
{
	memset(&params->masks[parameter], 0, sizeof(params->masks[parameter]));
	params->masks[parameter].bits[value / 32U] = 1U << (value % 32U);
	params->rmask |= 1U << parameter;
}

static void set_interval(struct snd_pcm_hw_params *params, u32 parameter,
			 u32 value)
{
	struct snd_interval *interval = param_interval(params, parameter);

	memset(interval, 0, sizeof(*interval));
	interval->min = value;
	interval->max = value;
	interval->integer = 1;
	params->rmask |= 1U << parameter;
}

static int configure_pcm(long fd)
{
	struct snd_pcm_hw_params params;
	long ret;

	params_any(&params);
	ret = syscall3(54, fd, SNDRV_PCM_IOCTL_HW_REFINE, (long)&params);
	if (ret < 0) {
		put_error("SNDRV_PCM_IOCTL_HW_REFINE", ret);
		return 1;
	}
	set_mask(&params, SNDRV_PCM_HW_PARAM_ACCESS,
		 SNDRV_PCM_ACCESS_RW_INTERLEAVED);
	set_mask(&params, SNDRV_PCM_HW_PARAM_FORMAT, SNDRV_PCM_FORMAT_S16_LE);
	set_mask(&params, SNDRV_PCM_HW_PARAM_SUBFORMAT, SNDRV_PCM_SUBFORMAT_STD);
	set_interval(&params, SNDRV_PCM_HW_PARAM_CHANNELS, PCM_CHANNELS);
	set_interval(&params, SNDRV_PCM_HW_PARAM_RATE, PCM_RATE);
	set_interval(&params, SNDRV_PCM_HW_PARAM_PERIOD_SIZE, PCM_PERIOD_FRAMES);
	set_interval(&params, SNDRV_PCM_HW_PARAM_PERIODS, PCM_PERIODS);
	set_interval(&params, SNDRV_PCM_HW_PARAM_BUFFER_SIZE, PCM_BUFFER_FRAMES);
	params.cmask = 0;
	ret = syscall3(54, fd, SNDRV_PCM_IOCTL_HW_PARAMS, (long)&params);
	if (ret < 0) {
		put_error("SNDRV_PCM_IOCTL_HW_PARAMS", ret);
		return 1;
	}
	ret = syscall3(54, fd, SNDRV_PCM_IOCTL_PREPARE, 0);
	if (ret < 0) {
		put_error("SNDRV_PCM_IOCTL_PREPARE", ret);
		return 1;
	}
	return 0;
}

struct channel_stats {
	u32 nonzero;
	u32 peak;
	u32 mean_square;
	u32 blocks;
};

static void update_stats(struct channel_stats *stats, const s16 *samples,
			 u32 frames, u32 channel)
{
	u32 square_sum = 0;
	u32 i;

	for (i = 0; i < frames; i++) {
		int sample = samples[i * PCM_CHANNELS + channel];
		u32 absolute = sample < 0 ? (u32)-sample : (u32)sample;
		u32 scaled = absolute >> 5;

		if (absolute)
			stats->nonzero++;
		if (absolute > stats->peak)
			stats->peak = absolute;
		square_sum += scaled * scaled;
	}
	if (frames) {
		u32 block_mean = divide_u32(square_sum, frames);

		stats->blocks++;
		if (block_mean >= stats->mean_square)
			stats->mean_square += divide_u32(block_mean - stats->mean_square,
						 stats->blocks);
		else
			stats->mean_square -= divide_u32(stats->mean_square - block_mean,
						 stats->blocks);
	}
}

static void print_channel(const char *name, const struct channel_stats *stats)
{
	putstr(name);
	putstr("_nonzero=");
	put_u32(stats->nonzero);
	putstr(" ");
	putstr(name);
	putstr("_peak=");
	put_u32(stats->peak);
	putstr(" ");
	putstr(name);
	putstr("_rms_approx=");
	put_u32(integer_sqrt(stats->mean_square) << 5);
	putstr("\n");
}

static __used __noinline int main(int argc, char **argv)
{
	struct channel_stats left = { 0 };
	struct channel_stats right = { 0 };
	u32 seconds = DEFAULT_SECONDS;
	u32 bytes_left;
	u32 frames = 0;
	u32 xruns = 0;
	long fd;

	if (argc > 2) {
		putstr("usage: r1-pcm-capture-test [seconds: 1..300]\n");
		return 2;
	}
	if (argc == 2) {
		seconds = parse_seconds(argv[1]);
		if (!seconds) {
			putstr("seconds must be in range 1..300\n");
			return 2;
		}
	}

	putstr("PRIVACY: capture statistics only; no PCM is stored or printed.\n");
	fd = syscall3(5, (long)"/dev/snd/pcmC0D0c", O_RDONLY, 0);
	if (fd < 0) {
		put_error("open /dev/snd/pcmC0D0c", fd);
		return 1;
	}
	if (configure_pcm(fd)) {
		(void)syscall1(6, fd);
		return 1;
	}
	putstr("capture=48kHz stereo S16_LE seconds=");
	put_u32(seconds);
	putstr(" state=running\n");

	bytes_left = seconds * PCM_RATE * PCM_FRAME_BYTES;
	while (bytes_left) {
		u32 chunk = bytes_left < PCM_PERIOD_BYTES ? bytes_left : PCM_PERIOD_BYTES;
		long ret = syscall3(3, fd, (long)capture_period, chunk);

		if (ret == -EPIPE) {
			xruns++;
			ret = syscall3(54, fd, SNDRV_PCM_IOCTL_PREPARE, 0);
			if (ret < 0) {
				put_error("capture xrun recovery", ret);
				(void)syscall1(6, fd);
				return 1;
			}
			continue;
		}
		if (ret < 0 || !ret || ((u32)ret % PCM_FRAME_BYTES)) {
			if (ret < 0)
				put_error("PCM capture read", ret);
			else
				putstr("PCM capture returned zero or partial frame\n");
			(void)syscall3(54, fd, SNDRV_PCM_IOCTL_DROP, 0);
			(void)syscall1(6, fd);
			return 1;
		}
		update_stats(&left, capture_period, (u32)ret / PCM_FRAME_BYTES, 0);
		update_stats(&right, capture_period, (u32)ret / PCM_FRAME_BYTES, 1);
		frames += (u32)ret / PCM_FRAME_BYTES;
		bytes_left -= (u32)ret;
	}

	(void)syscall3(54, fd, SNDRV_PCM_IOCTL_DROP, 0);
	(void)syscall1(6, fd);
	putstr("capture_frames=");
	put_u32(frames);
	putstr(" capture_xruns=");
	put_u32(xruns);
	putstr("\n");
	print_channel("left", &left);
	print_channel("right", &right);
	if (!left.nonzero && !right.nonzero) {
		putstr("capture_signal=absent\n");
		return 1;
	}
	putstr("capture_signal=present\n");
	return xruns ? 1 : 0;
}

__naked __noreturn void _start(void)
{
	__asm__ volatile(
		"mov r4, sp\n"
		"ldr r0, [r4]\n"
		"add r1, r4, #4\n"
		"bl main\n"
		"mov r7, #1\n"
		"svc 0\n"
		"b .\n"
	);
}
