// SPDX-License-Identifier: GPL-2.0-only
/* One-shot fail-closed -60 dBFS audible test for the Phicomm R1. */

typedef signed short s16;
typedef unsigned char u8;
typedef unsigned int u32;

#define __used __attribute__((used))
#define __noinline __attribute__((noinline))
#define __naked __attribute__((naked))
#define __noreturn __attribute__((noreturn))

#define O_WRONLY 1
#define O_RDWR 2
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

#define R1_AUDIO_IOC_ARM_MUTED 0x00005201U
#define R1_AUDIO_IOC_UNMUTE 0x00005202U
#define R1_AUDIO_IOC_SAFE 0x00005203U
#define R1_AUDIO_IOC_KEEPALIVE 0x00005204U

#define PCM_RATE 48000U
#define PCM_CHANNELS 2U
#define PCM_FRAME_BYTES 4U
#define PCM_PERIOD_FRAMES 1024U
#define PCM_PERIOD_BYTES (PCM_PERIOD_FRAMES * PCM_FRAME_BYTES)
#define PCM_PERIODS 4U
#define PCM_BUFFER_FRAMES (PCM_PERIOD_FRAMES * PCM_PERIODS)
#define TONE_FRAMES PCM_RATE
#define RAMP_FRAMES 4800U
#define FADE_START (TONE_FRAMES - RAMP_FRAMES)
#define RAMP_STEP_FRAMES 150U
#define TONE_PEAK 32U
#define ZERO_PREROLL_PERIODS 8U
#define ZERO_POSTROLL_PERIODS 4U

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

static s16 pcm_period[PCM_PERIOD_FRAMES * PCM_CHANNELS];
static const s16 sine_1khz[48] = {
	0, 4277, 8481, 12539, 16383, 19947, 23170, 25996,
	28377, 30273, 31650, 32487, 32767, 32487, 31650, 30273,
	28377, 25996, 23170, 19947, 16383, 12539, 8481, 4277,
	0, -4277, -8481, -12539, -16383, -19947, -23170, -25996,
	-28377, -30273, -31650, -32487, -32767, -32487, -31650, -30273,
	-28377, -25996, -23170, -19947, -16384, -12539, -8481, -4277,
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

void *memcpy(void *dst, const void *src, u32 len)
{
	u8 *out = dst;
	const u8 *in = src;
	u32 i;

	for (i = 0; i < len; i++)
		out[i] = in[i];
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

static void put_error(const char *operation, long error)
{
	static const char hex[] = "0123456789abcdef";
	char out[] = " errno=0x00";
	u32 value = (u32)-error;

	out[9] = hex[(value >> 4) & 0xf];
	out[10] = hex[value & 0xf];
	putstr(operation);
	(void)syscall3(4, 1, (long)out, sizeof(out) - 1U);
	putstr("\n");
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

static int safety_command(long safety_fd, u32 command, const char *label)
{
	long ret = syscall3(54, safety_fd, command, 0);

	if (ret < 0) {
		put_error(label, ret);
		return 1;
	}
	return 0;
}

static int write_period(long pcm_fd, u32 frames)
{
	u32 bytes = frames * PCM_FRAME_BYTES;
	u32 done = 0;

	while (done < bytes) {
		long ret = syscall3(4, pcm_fd, (long)((u8 *)pcm_period + done),
				    bytes - done);

		if (ret == -EPIPE) {
			putstr("FAIL: audible PCM xrun\n");
			return 1;
		}
		if (ret <= 0) {
			if (ret < 0)
				put_error("audible PCM write", ret);
			else
				putstr("audible PCM write returned zero\n");
			return 1;
		}
		done += (u32)ret;
	}
	return 0;
}

static void fill_tone(u32 first_frame, u32 frames)
{
	u32 amplitude = TONE_PEAK;
	u32 ramp_value = 0;
	u32 ramp_tick = 0;
	u32 phase = first_frame;
	u32 i;

	while (phase >= 48U)
		phase -= 48U;

	/* Reconstruct the deterministic ramp state for this period. */
	for (i = 0; i < first_frame && i < RAMP_FRAMES; i++) {
		if (++ramp_tick == RAMP_STEP_FRAMES) {
			ramp_tick = 0;
			if (ramp_value < TONE_PEAK)
				ramp_value++;
		}
	}

	for (i = 0; i < frames; i++) {
		u32 frame = first_frame + i;
		s16 sample;

		if (frame < RAMP_FRAMES) {
			amplitude = ramp_value;
			if (++ramp_tick == RAMP_STEP_FRAMES) {
				ramp_tick = 0;
				if (ramp_value < TONE_PEAK)
					ramp_value++;
			}
		} else if (frame >= FADE_START) {
			u32 remaining = TONE_FRAMES - frame;
			/* 150-frame steps, implemented without a runtime divide. */
			amplitude = 0;
			while (remaining >= RAMP_STEP_FRAMES &&
			       amplitude < TONE_PEAK) {
				remaining -= RAMP_STEP_FRAMES;
				amplitude++;
			}
		} else {
			amplitude = TONE_PEAK;
		}

		sample = (s16)(((int)sine_1khz[phase] *
				amplitude) >> 15);
		pcm_period[i * 2U] = sample;
		pcm_period[i * 2U + 1U] = sample;
		if (++phase == 48U)
			phase = 0;
	}
}

static __used __noinline int main(int argc, char **argv)
{
	long safety_fd = -1;
	long pcm_fd = -1;
	u32 frame = 0;
	u32 i;
	int ret = 1;

	(void)argv;

	if (argc != 1) {
		putstr("usage: r1-audible-test\n");
		return 2;
	}

	putstr("WARNING: audible output test; keep away from the speaker.\n");
	putstr("Fixed signal: 1 kHz stereo, about -60 dBFS, 100 ms fade, 1 second.\n");
	putstr("Kernel fail-safe: exclusive gate, close-to-safe, 500 ms keepalive.\n");

	safety_fd = syscall3(5, (long)"/dev/r1-audio-safety", O_RDWR, 0);
	if (safety_fd < 0) {
		put_error("open /dev/r1-audio-safety", safety_fd);
		return 1;
	}
	if (safety_command(safety_fd, R1_AUDIO_IOC_SAFE, "initial SAFE"))
		goto out;
	if (safety_command(safety_fd, R1_AUDIO_IOC_ARM_MUTED, "ARM_MUTED"))
		goto out;

	pcm_fd = syscall3(5, (long)"/dev/snd/pcmC0D0p", O_WRONLY, 0);
	if (pcm_fd < 0) {
		put_error("open playback PCM", pcm_fd);
		goto out;
	}
	if (configure_pcm(pcm_fd))
		goto out;

	memset(pcm_period, 0, sizeof(pcm_period));
	for (i = 0; i < ZERO_PREROLL_PERIODS; i++)
		if (write_period(pcm_fd, PCM_PERIOD_FRAMES))
			goto out;

	if (safety_command(safety_fd, R1_AUDIO_IOC_UNMUTE, "UNMUTE"))
		goto out;
	putstr("audible_window=started\n");

	while (frame < TONE_FRAMES) {
		u32 frames = TONE_FRAMES - frame;

		if (frames > PCM_PERIOD_FRAMES)
			frames = PCM_PERIOD_FRAMES;
		if (safety_command(safety_fd, R1_AUDIO_IOC_KEEPALIVE,
				   "KEEPALIVE"))
			goto out;
		fill_tone(frame, frames);
		if (write_period(pcm_fd, frames))
			goto out;
		frame += frames;
	}

	memset(pcm_period, 0, sizeof(pcm_period));
	for (i = 0; i < ZERO_POSTROLL_PERIODS; i++) {
		if (safety_command(safety_fd, R1_AUDIO_IOC_KEEPALIVE,
				   "KEEPALIVE"))
			goto out;
		if (write_period(pcm_fd, PCM_PERIOD_FRAMES))
			goto out;
	}

	if (safety_command(safety_fd, R1_AUDIO_IOC_SAFE, "final SAFE"))
		goto out;
	putstr("audible_window=complete result=PASS\n");
	ret = 0;

out:
	if (safety_fd >= 0)
		(void)syscall3(54, safety_fd, R1_AUDIO_IOC_SAFE, 0);
	if (pcm_fd >= 0) {
		(void)syscall3(54, pcm_fd, SNDRV_PCM_IOCTL_DROP, 0);
		(void)syscall1(6, pcm_fd);
	}
	if (safety_fd >= 0)
		(void)syscall1(6, safety_fd);
	return ret;
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
