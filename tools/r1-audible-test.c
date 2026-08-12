// SPDX-License-Identifier: GPL-2.0-only
/* One-shot fail-closed -60 dBFS audible tests for the Phicomm R1. */

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
#define R1_AUDIO_IOC_DAC_MUTE 0x00005205U
#define R1_AUDIO_IOC_DAC_UNMUTE 0x00005206U
#define R1_AUDIO_IOC_DAC_ANALOG_OFF 0x00005207U
#define R1_AUDIO_IOC_LINEOUT_HIZ 0x00005208U
#define R1_AUDIO_IOC_LINEOUT_MINUS14DB 0x00005209U
#define R1_AUDIO_IOC_LINEOUT_MINUS28DB 0x0000520aU

#define PCM_RATE 48000U
#define PCM_CHANNELS 2U
#define PCM_FRAME_BYTES 4U
#define PCM_PERIOD_FRAMES 1024U
#define PCM_PERIOD_BYTES (PCM_PERIOD_FRAMES * PCM_FRAME_BYTES)
#ifdef R1_LINEOUT_SELFCHECK_TEST
/* Cover the bounded DAC-mute + D4-write + DAC-unmute control interval. */
#define PCM_PERIODS 16U
#else
#define PCM_PERIODS 4U
#endif
#define PCM_BUFFER_FRAMES (PCM_PERIOD_FRAMES * PCM_PERIODS)
#ifdef R1_SWEEP_TEST
#define SWEEP_FRAMES 65536U
#define SWEEP_GAP_FRAMES 8192U
#define SWEEP_PASS_FRAMES (SWEEP_FRAMES + SWEEP_GAP_FRAMES)
#define SWEEP_PASS_COUNT 3U
#define TONE_FRAMES (SWEEP_PASS_FRAMES * SWEEP_PASS_COUNT)
#elif defined(R1_MUSIC_TEST)
#define MUSIC_NOTE_FRAMES 32768U
#define MUSIC_NOTE_COUNT 16U
#define MUSIC_PEAK 512U
#define TONE_FRAMES (MUSIC_NOTE_FRAMES * MUSIC_NOTE_COUNT)
#elif defined(R1_MELODY_TEST)
#define MELODY_NOTE_FRAMES 16384U
#define MELODY_NOTE_COUNT 14U
#define TONE_FRAMES (MELODY_NOTE_FRAMES * MELODY_NOTE_COUNT)
#elif defined(R1_CHANNEL_TEST)
#define TONE_FRAMES 36000U
#define CHANNEL_GAP_PERIODS 144U
#else
#define TONE_FRAMES PCM_RATE
#endif
#if defined(R1_MUTE_AB_TEST) || defined(R1_DAC_MUTE_AB_TEST) || \
	defined(R1_ANALOG_BOUNDARY_TEST) || defined(R1_LINEOUT_VOLUME_TEST) || \
	defined(R1_LINEOUT_SELFCHECK_TEST) || defined(R1_I2S_CLOCK_AB_TEST)
#define MUTE_AB_ZERO_PERIODS 94U
#endif
#define RAMP_FRAMES 4800U
#define FADE_START (TONE_FRAMES - RAMP_FRAMES)
#define RAMP_STEP_FRAMES 150U
#ifdef R1_LINEOUT_SELFCHECK_TEST
#define TONE_PEAK 512U
#else
#define TONE_PEAK 32U
#endif
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

struct timespec32 {
	long tv_sec;
	long tv_nsec;
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

static __used long syscall2(long nr, long a0, long a1)
{
	register long r0 __asm__("r0") = a0;
	register long r1 __asm__("r1") = a1;
	register long r7 __asm__("r7") = nr;

	__asm__ volatile("svc 0" : "+r"(r0) : "r"(r1), "r"(r7) : "memory");
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

#ifdef R1_SWEEP_TEST
static void fill_sweep(u32 frames)
{
#ifdef R1_SWEEP_HIGH_TEST
	static const u32 peaks[SWEEP_PASS_COUNT] = { 128U, 256U, 512U };
#else
	static const u32 peaks[SWEEP_PASS_COUNT] = { 32U, 64U, 128U };
#endif
	static u32 pass;
	static u32 position;
	static u32 phase_q16;
	static u32 phase_step_q16 = 19661U;
	static u32 step_error;
	u32 i;

	for (i = 0; i < frames; i++) {
		u32 amplitude = 0;
		s16 sample = 0;

		if (position < SWEEP_FRAMES) {
			u32 peak = peaks[pass];

			if (position < 2048U)
				amplitude = (peak * position) >> 11;
			else if (position >= SWEEP_FRAMES - 2048U)
				amplitude = (peak * (SWEEP_FRAMES - position)) >> 11;
			else
				amplitude = peak;

			sample = (s16)(((int)sine_1khz[phase_q16 >> 16] *
					amplitude) >> 15);
			phase_q16 += phase_step_q16;
			while (phase_q16 >= (48U << 16))
				phase_q16 -= 48U << 16;

			/* 300 Hz to 2 kHz across exactly 65536 frames. */
			phase_step_q16++;
			step_error += 45875U;
			if (step_error >= 65536U) {
				step_error -= 65536U;
				phase_step_q16++;
			}
		}

		pcm_period[i * 2U] = sample;
		pcm_period[i * 2U + 1U] = sample;
		position++;
		if (position == SWEEP_PASS_FRAMES) {
			position = 0;
			pass++;
			phase_q16 = 0;
			phase_step_q16 = 19661U;
			step_error = 0;
		}
	}
}
#elif defined(R1_MUSIC_TEST)
/*
 * Public-domain "Ode to Joy" opening phrase.  It is synthesized locally so
 * the rescue image never accepts or bundles an arbitrary media payload.
 */
static void fill_music(u32 first_frame, u32 frames)
{
	static const u32 phase_steps[MUSIC_NOTE_COUNT] = {
		21603U, 21603U, 22887U, 25690U,
		25690U, 22887U, 21603U, 19245U,
		17146U, 17146U, 19245U, 21603U,
		21603U, 19245U, 19245U, 19245U,
	};
	static u32 current_note = ~0U;
	static u32 phase_q16;
	static u32 harmonic_phase_q16;
	u32 i;

	for (i = 0; i < frames; i++) {
		u32 frame = first_frame + i;
		u32 note = frame >> 15;
		u32 position = frame & (MUSIC_NOTE_FRAMES - 1U);
		u32 amplitude;
		int fundamental;
		int harmonic;
		s16 sample;

		if (note != current_note) {
			current_note = note;
			phase_q16 = 0;
			harmonic_phase_q16 = 0;
		}
		if (position < 2048U)
			amplitude = (MUSIC_PEAK * position) >> 11;
		else if (position >= 28672U)
			amplitude = 0;
		else if (position >= 26624U)
			amplitude = (MUSIC_PEAK * (28672U - position)) >> 11;
		else
			amplitude = MUSIC_PEAK;

		/* 3:1 fundamental/second-harmonic mix, bounded by MUSIC_PEAK. */
		fundamental = ((int)sine_1khz[phase_q16 >> 16] *
			       (int)(amplitude * 3U)) >> 17;
		harmonic = ((int)sine_1khz[harmonic_phase_q16 >> 16] *
			    (int)amplitude) >> 17;
		sample = (s16)(fundamental + harmonic);
		pcm_period[i * 2U] = sample;
		pcm_period[i * 2U + 1U] = sample;

		phase_q16 += phase_steps[note];
		harmonic_phase_q16 += phase_steps[note] << 1;
		while (phase_q16 >= (48U << 16))
			phase_q16 -= 48U << 16;
		while (harmonic_phase_q16 >= (48U << 16))
			harmonic_phase_q16 -= 48U << 16;
	}
}
#elif defined(R1_MELODY_TEST)
static void fill_melody(u32 first_frame, u32 frames)
{
	static const u32 phase_steps[MELODY_NOTE_COUNT] = {
		17146U, 17146U, 25690U, 25690U, 28836U, 28836U, 25690U,
		22887U, 22887U, 21603U, 21603U, 19245U, 19245U, 17146U,
	};
	static u32 current_note = ~0U;
	static u32 phase_q16;
	u32 i;

	for (i = 0; i < frames; i++) {
		u32 frame = first_frame + i;
		u32 note = frame >> 14;
		u32 position = frame & (MELODY_NOTE_FRAMES - 1U);
		u32 amplitude;
		s16 sample;

		if (note != current_note) {
			current_note = note;
			phase_q16 = 0;
		}
		if (position < 512U)
			amplitude = position >> 4;
		else if (position >= 14336U)
			amplitude = 0;
		else if (position >= 13824U)
			amplitude = (14336U - position) >> 4;
		else
			amplitude = TONE_PEAK;

		sample = (s16)(((int)sine_1khz[phase_q16 >> 16] *
				amplitude) >> 15);
		pcm_period[i * 2U] = sample;
		pcm_period[i * 2U + 1U] = sample;
		phase_q16 += phase_steps[note];
		while (phase_q16 >= (48U << 16))
			phase_q16 -= 48U << 16;
	}
}
#else
static __used void fill_tone(u32 first_frame, u32 frames, u32 channel_mask)
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
		pcm_period[i * 2U] = channel_mask & 1U ? sample : 0;
		pcm_period[i * 2U + 1U] = channel_mask & 2U ? sample : 0;
		if (++phase == 48U)
			phase = 0;
	}
}
#endif

static __used __noinline int main(int argc, char **argv)
{
	long safety_fd = -1;
	long pcm_fd = -1;
	u32 frame = 0;
	u32 i;
	int ret = 1;

	(void)argv;
	(void)frame;

	if (argc != 1) {
#ifdef R1_SWEEP_TEST
		putstr("usage: r1-sweep-test\n");
#elif defined(R1_MUSIC_TEST)
		putstr("usage: r1-music-test\n");
#elif defined(R1_DAC_MUTE_AB_TEST)
		putstr("usage: r1-dac-mute-ab\n");
#elif defined(R1_ANALOG_BOUNDARY_TEST)
		putstr("usage: r1-analog-boundary-ab\n");
	#elif defined(R1_LINEOUT_SELFCHECK_TEST)
		putstr("usage: r1-lineout-selfcheck\n");
	#elif defined(R1_I2S_CLOCK_AB_TEST)
		putstr("usage: r1-i2s-clock-ab\n");
#elif defined(R1_LINEOUT_VOLUME_TEST)
		putstr("usage: r1-lineout-volume-ab\n");
#elif defined(R1_MELODY_TEST)
		putstr("usage: r1-melody-test\n");
#elif defined(R1_MUTE_AB_TEST)
		putstr("usage: r1-audio-mute-ab\n");
#elif defined(R1_CHANNEL_TEST)
		putstr("usage: r1-channel-test\n");
#else
		putstr("usage: r1-audible-test\n");
#endif
		return 2;
	}

#ifdef R1_SWEEP_TEST
	putstr("WARNING: controlled three-level audible sweep test.\n");
#ifdef R1_SWEEP_HIGH_TEST
	putstr("Signal: 300 Hz to 2 kHz at -48, -42, then -36 dBFS.\n");
#else
	putstr("Signal: 300 Hz to 2 kHz at -60, -54, then -48 dBFS.\n");
#endif
	putstr("Stop immediately if unexpectedly loud; kernel fail-safe remains active.\n");
#elif defined(R1_MUSIC_TEST)
	putstr("WARNING: controlled public-domain music playback test.\n");
	putstr("Signal: synthesized Ode to Joy phrase, about -36 dBFS, 10.9 seconds.\n");
	putstr("Stop immediately if unexpectedly loud; no external media is accepted.\n");
#elif defined(R1_DAC_MUTE_AB_TEST)
	putstr("WARNING: AK7755 DAC soft-mute noise-boundary diagnostic.\n");
	putstr("Signal: -60 dBFS reference, then zero PCM with DAC unmuted/muted/unmuted.\n");
	putstr("TPA3118 remains enabled and unmuted during all three zero windows.\n");
#elif defined(R1_ANALOG_BOUNDARY_TEST)
	putstr("WARNING: AK7755 analog power-boundary noise diagnostic.\n");
	putstr("Signal: -60 dBFS reference, then zero PCM through three analog states.\n");
	putstr("TPA3118 remains enabled and unmuted; stop if transitions are unexpectedly loud.\n");
	#elif defined(R1_LINEOUT_SELFCHECK_TEST)
		putstr("WARNING: AK7755 Lineout1 D4 audible self-check.\n");
		putstr("Signal: identical 1 kHz about -36 dBFS, then zero, at 0/-14/-28 dB.\n");
		putstr("DAC is muted only during D4 transitions; TPA3118 stays fail-safe controlled.\n");
	#elif defined(R1_I2S_CLOCK_AB_TEST)
		putstr("WARNING: AK7755 I2S clock noise-boundary diagnostic.\n");
		putstr("Signal is digital zero; compare I2S running/stopped/running windows.\n");
		putstr("Codec route and TPA3118 remain enabled+unmuted across PCM DROP.\n");
#elif defined(R1_LINEOUT_VOLUME_TEST)
	putstr("WARNING: AK7755 Lineout1 analog-volume noise diagnostic.\n");
	putstr("Signal: -60 dBFS reference, then DAC-muted zero PCM at 0/-14/-28 dB.\n");
	putstr("Lineout stays low-impedance and TPA3118 stays enabled+unmuted.\n");
#elif defined(R1_MELODY_TEST)
	putstr("WARNING: fixed low-level synthesized melody test.\n");
	putstr("Signal: 4.8 s C-major phrase, about -60 dBFS; no external audio file.\n");
#elif defined(R1_MUTE_AB_TEST)
	putstr("WARNING: fixed low-level amplifier MUTE A/B diagnostic.\n");
	putstr("Signal: 1 kHz reference, then three 2.0 s digital-zero windows.\n");
#elif defined(R1_CHANNEL_TEST)
	putstr("WARNING: fixed low-level left/right channel test.\n");
	putstr("Signal: left 750 ms, silence 3072 ms, right 750 ms; 1 kHz, about -60 dBFS.\n");
#else
	putstr("WARNING: audible output test; keep away from the speaker.\n");
	putstr("Fixed signal: 1 kHz stereo, about -60 dBFS, 100 ms fade, 1 second.\n");
#endif
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
#ifdef R1_I2S_CLOCK_AB_TEST
	memset(pcm_period, 0, sizeof(pcm_period));
	putstr("zero_i2s_running_1=started seconds=2.0\n");
	for (i = 0; i < MUTE_AB_ZERO_PERIODS; i++) {
		if (safety_command(safety_fd, R1_AUDIO_IOC_KEEPALIVE,
				   "KEEPALIVE"))
			goto out;
		if (write_period(pcm_fd, PCM_PERIOD_FRAMES))
			goto out;
	}
	putstr("zero_i2s_running_1=complete\n");

	if (syscall3(54, pcm_fd, SNDRV_PCM_IOCTL_DROP, 0) < 0) {
		putstr("SNDRV_PCM_IOCTL_DROP failed\n");
		goto out;
	}
	putstr("zero_i2s_stopped=started seconds=2.0 amp=unmuted\n");
	for (i = 0; i < 20U; i++) {
		struct timespec32 delay = { 0, 100000000 };

		if (safety_command(safety_fd, R1_AUDIO_IOC_KEEPALIVE,
				   "KEEPALIVE"))
			goto out;
		if (syscall2(162, (long)&delay, 0) < 0) {
			putstr("nanosleep failed\n");
			goto out;
		}
	}
	putstr("zero_i2s_stopped=complete\n");

	if (syscall3(54, pcm_fd, SNDRV_PCM_IOCTL_PREPARE, 0) < 0) {
		putstr("SNDRV_PCM_IOCTL_PREPARE restart failed\n");
		goto out;
	}
	putstr("zero_i2s_running_2=started seconds=2.0\n");
	for (i = 0; i < MUTE_AB_ZERO_PERIODS; i++) {
		if (safety_command(safety_fd, R1_AUDIO_IOC_KEEPALIVE,
				   "KEEPALIVE"))
			goto out;
		if (write_period(pcm_fd, PCM_PERIOD_FRAMES))
			goto out;
	}
	putstr("zero_i2s_running_2=complete\n");
#else
#ifdef R1_SWEEP_TEST
#ifdef R1_SWEEP_HIGH_TEST
	putstr("sweep_level=-48dBFS started\n");
#else
	putstr("sweep_level=-60dBFS started\n");
#endif
#elif defined(R1_MUSIC_TEST)
	putstr("music_window=started title=ode_to_joy_public_domain peak=-36dBFS\n");
#elif defined(R1_DAC_MUTE_AB_TEST)
	putstr("reference_1khz=started\n");
#elif defined(R1_ANALOG_BOUNDARY_TEST)
	putstr("reference_1khz=started\n");
#elif defined(R1_LINEOUT_SELFCHECK_TEST)
	putstr("stage_0db_tone=started peak=-36dBFS\n");
#elif defined(R1_LINEOUT_VOLUME_TEST)
	putstr("reference_1khz=started\n");
#elif defined(R1_MELODY_TEST)
	putstr("melody_window=started\n");
#elif defined(R1_MUTE_AB_TEST)
	putstr("reference_1khz=started\n");
#elif defined(R1_CHANNEL_TEST)
	putstr("channel_window=left started\n");
#else
	putstr("audible_window=started\n");
#endif

	while (frame < TONE_FRAMES) {
		u32 frames = TONE_FRAMES - frame;

		if (frames > PCM_PERIOD_FRAMES)
			frames = PCM_PERIOD_FRAMES;
		if (safety_command(safety_fd, R1_AUDIO_IOC_KEEPALIVE,
				   "KEEPALIVE"))
			goto out;
#ifdef R1_SWEEP_TEST
		if (frame == SWEEP_PASS_FRAMES)
#ifdef R1_SWEEP_HIGH_TEST
			putstr("sweep_level=-42dBFS started\n");
		else if (frame == SWEEP_PASS_FRAMES * 2U)
			putstr("sweep_level=-36dBFS started\n");
#else
			putstr("sweep_level=-54dBFS started\n");
		else if (frame == SWEEP_PASS_FRAMES * 2U)
			putstr("sweep_level=-48dBFS started\n");
#endif
		fill_sweep(frames);
#elif defined(R1_MUSIC_TEST)
		fill_music(frame, frames);
#elif defined(R1_MELODY_TEST)
		fill_melody(frame, frames);
#elif defined(R1_CHANNEL_TEST)
		fill_tone(frame, frames, 1U);
#else
		fill_tone(frame, frames, 3U);
#endif
		if (write_period(pcm_fd, frames))
			goto out;
		frame += frames;
	}

#if defined(R1_MUTE_AB_TEST) || defined(R1_DAC_MUTE_AB_TEST) || \
	defined(R1_ANALOG_BOUNDARY_TEST) || defined(R1_LINEOUT_VOLUME_TEST) || \
	defined(R1_LINEOUT_SELFCHECK_TEST)
#if defined(R1_LINEOUT_SELFCHECK_TEST)
	putstr("stage_0db_tone=complete\n");
	memset(pcm_period, 0, sizeof(pcm_period));
	putstr("stage_0db_zero=started seconds=2.0\n");
	for (i = 0; i < MUTE_AB_ZERO_PERIODS; i++) {
		if (safety_command(safety_fd, R1_AUDIO_IOC_KEEPALIVE,
				   "KEEPALIVE"))
			goto out;
		if (write_period(pcm_fd, PCM_PERIOD_FRAMES))
			goto out;
	}
	putstr("stage_0db_zero=complete\n");

	if (safety_command(safety_fd, R1_AUDIO_IOC_DAC_MUTE, "DAC_MUTE"))
		goto out;
	if (safety_command(safety_fd, R1_AUDIO_IOC_LINEOUT_MINUS14DB,
			   "LINEOUT_MINUS14DB"))
		goto out;
	if (safety_command(safety_fd, R1_AUDIO_IOC_DAC_UNMUTE,
			   "DAC_UNMUTE"))
		goto out;
	putstr("stage_minus14db_tone=started peak=-36dBFS\n");
	frame = 0;
	while (frame < TONE_FRAMES) {
		u32 frames = TONE_FRAMES - frame;

		if (frames > PCM_PERIOD_FRAMES)
			frames = PCM_PERIOD_FRAMES;
		if (safety_command(safety_fd, R1_AUDIO_IOC_KEEPALIVE,
				   "KEEPALIVE"))
			goto out;
		fill_tone(frame, frames, 3U);
		if (write_period(pcm_fd, frames))
			goto out;
		frame += frames;
	}
	putstr("stage_minus14db_tone=complete\n");
	memset(pcm_period, 0, sizeof(pcm_period));
	putstr("stage_minus14db_zero=started seconds=2.0\n");
	for (i = 0; i < MUTE_AB_ZERO_PERIODS; i++) {
		if (safety_command(safety_fd, R1_AUDIO_IOC_KEEPALIVE,
				   "KEEPALIVE"))
			goto out;
		if (write_period(pcm_fd, PCM_PERIOD_FRAMES))
			goto out;
	}
	putstr("stage_minus14db_zero=complete\n");

	if (safety_command(safety_fd, R1_AUDIO_IOC_DAC_MUTE, "DAC_MUTE"))
		goto out;
	if (safety_command(safety_fd, R1_AUDIO_IOC_LINEOUT_MINUS28DB,
			   "LINEOUT_MINUS28DB"))
		goto out;
	if (safety_command(safety_fd, R1_AUDIO_IOC_DAC_UNMUTE,
			   "DAC_UNMUTE"))
		goto out;
	putstr("stage_minus28db_tone=started peak=-36dBFS\n");
	frame = 0;
	while (frame < TONE_FRAMES) {
		u32 frames = TONE_FRAMES - frame;

		if (frames > PCM_PERIOD_FRAMES)
			frames = PCM_PERIOD_FRAMES;
		if (safety_command(safety_fd, R1_AUDIO_IOC_KEEPALIVE,
				   "KEEPALIVE"))
			goto out;
		fill_tone(frame, frames, 3U);
		if (write_period(pcm_fd, frames))
			goto out;
		frame += frames;
	}
	putstr("stage_minus28db_tone=complete\n");
	memset(pcm_period, 0, sizeof(pcm_period));
	putstr("stage_minus28db_zero=started seconds=2.0\n");
	for (i = 0; i < MUTE_AB_ZERO_PERIODS; i++) {
		if (safety_command(safety_fd, R1_AUDIO_IOC_KEEPALIVE,
				   "KEEPALIVE"))
			goto out;
		if (write_period(pcm_fd, PCM_PERIOD_FRAMES))
			goto out;
	}
	putstr("stage_minus28db_zero=complete\n");
#else
	putstr("reference_1khz=complete\n");
	memset(pcm_period, 0, sizeof(pcm_period));
#if defined(R1_ANALOG_BOUNDARY_TEST)
	if (safety_command(safety_fd, R1_AUDIO_IOC_DAC_MUTE,
			   "DAC_MUTE"))
		goto out;
	putstr("zero_dac_muted=started seconds=2.0 amp=unmuted\n");
	for (i = 0; i < MUTE_AB_ZERO_PERIODS; i++) {
		if (safety_command(safety_fd, R1_AUDIO_IOC_KEEPALIVE,
				   "KEEPALIVE"))
			goto out;
		if (write_period(pcm_fd, PCM_PERIOD_FRAMES))
			goto out;
	}
	putstr("zero_dac_muted=complete\n");

	if (safety_command(safety_fd, R1_AUDIO_IOC_DAC_ANALOG_OFF,
			   "DAC_ANALOG_OFF"))
		goto out;
	putstr("zero_dac_off_lineout_vmid=started seconds=2.0 amp=unmuted\n");
	for (i = 0; i < MUTE_AB_ZERO_PERIODS; i++) {
		if (safety_command(safety_fd, R1_AUDIO_IOC_KEEPALIVE,
				   "KEEPALIVE"))
			goto out;
		if (write_period(pcm_fd, PCM_PERIOD_FRAMES))
			goto out;
	}
	putstr("zero_dac_off_lineout_vmid=complete\n");

	if (safety_command(safety_fd, R1_AUDIO_IOC_LINEOUT_HIZ,
			   "LINEOUT_HIZ"))
		goto out;
	putstr("zero_lineout_hiz=started seconds=2.0 amp=unmuted\n");
	for (i = 0; i < MUTE_AB_ZERO_PERIODS; i++) {
		if (safety_command(safety_fd, R1_AUDIO_IOC_KEEPALIVE,
				   "KEEPALIVE"))
			goto out;
		if (write_period(pcm_fd, PCM_PERIOD_FRAMES))
			goto out;
	}
	putstr("zero_lineout_hiz=complete\n");
#elif defined(R1_LINEOUT_VOLUME_TEST)
	if (safety_command(safety_fd, R1_AUDIO_IOC_DAC_MUTE,
			   "DAC_MUTE"))
		goto out;
	putstr("zero_lineout_0db=started seconds=2.0 dac=muted amp=unmuted\n");
	for (i = 0; i < MUTE_AB_ZERO_PERIODS; i++) {
		if (safety_command(safety_fd, R1_AUDIO_IOC_KEEPALIVE,
				   "KEEPALIVE"))
			goto out;
		if (write_period(pcm_fd, PCM_PERIOD_FRAMES))
			goto out;
	}
	putstr("zero_lineout_0db=complete\n");

	if (safety_command(safety_fd, R1_AUDIO_IOC_LINEOUT_MINUS14DB,
			   "LINEOUT_MINUS14DB"))
		goto out;
	putstr("zero_lineout_minus14db=started seconds=2.0 dac=muted amp=unmuted\n");
	for (i = 0; i < MUTE_AB_ZERO_PERIODS; i++) {
		if (safety_command(safety_fd, R1_AUDIO_IOC_KEEPALIVE,
				   "KEEPALIVE"))
			goto out;
		if (write_period(pcm_fd, PCM_PERIOD_FRAMES))
			goto out;
	}
	putstr("zero_lineout_minus14db=complete\n");

	if (safety_command(safety_fd, R1_AUDIO_IOC_LINEOUT_MINUS28DB,
			   "LINEOUT_MINUS28DB"))
		goto out;
	putstr("zero_lineout_minus28db=started seconds=2.0 dac=muted amp=unmuted\n");
	for (i = 0; i < MUTE_AB_ZERO_PERIODS; i++) {
		if (safety_command(safety_fd, R1_AUDIO_IOC_KEEPALIVE,
				   "KEEPALIVE"))
			goto out;
		if (write_period(pcm_fd, PCM_PERIOD_FRAMES))
			goto out;
	}
	putstr("zero_lineout_minus28db=complete\n");
#else
	putstr("zero_unmuted_1=started seconds=2.0\n");
	for (i = 0; i < MUTE_AB_ZERO_PERIODS; i++) {
		if (safety_command(safety_fd, R1_AUDIO_IOC_KEEPALIVE,
				   "KEEPALIVE"))
			goto out;
		if (write_period(pcm_fd, PCM_PERIOD_FRAMES))
			goto out;
	}
	putstr("zero_unmuted_1=complete\n");

#ifdef R1_DAC_MUTE_AB_TEST
	if (safety_command(safety_fd, R1_AUDIO_IOC_DAC_MUTE,
			   "DAC_MUTE"))
		goto out;
	putstr("zero_dac_muted=started seconds=2.0 amp=unmuted\n");
	for (i = 0; i < MUTE_AB_ZERO_PERIODS; i++) {
		if (safety_command(safety_fd, R1_AUDIO_IOC_KEEPALIVE,
				   "KEEPALIVE"))
			goto out;
		if (write_period(pcm_fd, PCM_PERIOD_FRAMES))
			goto out;
	}
	putstr("zero_dac_muted=complete\n");

	if (safety_command(safety_fd, R1_AUDIO_IOC_DAC_UNMUTE,
			   "DAC_UNMUTE"))
		goto out;
#else
	if (safety_command(safety_fd, R1_AUDIO_IOC_ARM_MUTED,
			   "ARM_MUTED A/B"))
		goto out;
	putstr("zero_hardware_muted=started seconds=2.0\n");
	for (i = 0; i < MUTE_AB_ZERO_PERIODS; i++)
		if (write_period(pcm_fd, PCM_PERIOD_FRAMES))
			goto out;
	putstr("zero_hardware_muted=complete\n");

	if (safety_command(safety_fd, R1_AUDIO_IOC_UNMUTE,
			   "UNMUTE A/B"))
		goto out;
#endif
	putstr("zero_unmuted_2=started seconds=2.0\n");
	for (i = 0; i < MUTE_AB_ZERO_PERIODS; i++) {
		if (safety_command(safety_fd, R1_AUDIO_IOC_KEEPALIVE,
				   "KEEPALIVE"))
			goto out;
		if (write_period(pcm_fd, PCM_PERIOD_FRAMES))
			goto out;
	}
	putstr("zero_unmuted_2=complete\n");
#endif
#endif
#elif defined(R1_CHANNEL_TEST)
	putstr("channel_window=left complete; silence\n");
	memset(pcm_period, 0, sizeof(pcm_period));
	for (i = 0; i < CHANNEL_GAP_PERIODS; i++) {
		if (safety_command(safety_fd, R1_AUDIO_IOC_KEEPALIVE,
				   "KEEPALIVE"))
			goto out;
		if (write_period(pcm_fd, PCM_PERIOD_FRAMES))
			goto out;
	}
	putstr("channel_window=right started\n");
	frame = 0;
	while (frame < TONE_FRAMES) {
		u32 frames = TONE_FRAMES - frame;

		if (frames > PCM_PERIOD_FRAMES)
			frames = PCM_PERIOD_FRAMES;
		if (safety_command(safety_fd, R1_AUDIO_IOC_KEEPALIVE,
				   "KEEPALIVE"))
			goto out;
		fill_tone(frame, frames, 2U);
		if (write_period(pcm_fd, frames))
			goto out;
		frame += frames;
	}
	putstr("channel_window=right complete\n");
#endif
#endif /* R1_I2S_CLOCK_AB_TEST */

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
#ifdef R1_SWEEP_TEST
	putstr("sweep_test=complete result=PASS\n");
#elif defined(R1_MUSIC_TEST)
	putstr("music_test=complete result=PASS\n");
#elif defined(R1_DAC_MUTE_AB_TEST)
	putstr("dac_mute_ab_test=complete result=PASS\n");
#elif defined(R1_ANALOG_BOUNDARY_TEST)
	putstr("analog_boundary_test=complete result=PASS\n");
	#elif defined(R1_LINEOUT_SELFCHECK_TEST)
		putstr("lineout_selfcheck=complete result=PASS\n");
	#elif defined(R1_I2S_CLOCK_AB_TEST)
		putstr("i2s_clock_ab=complete result=PASS\n");
#elif defined(R1_LINEOUT_VOLUME_TEST)
	putstr("lineout_volume_ab_test=complete result=PASS\n");
#elif defined(R1_MELODY_TEST)
	putstr("melody_test=complete result=PASS\n");
#elif defined(R1_MUTE_AB_TEST)
	putstr("mute_ab_test=complete result=PASS\n");
#elif defined(R1_CHANNEL_TEST)
	putstr("channel_test=complete result=PASS\n");
#else
	putstr("audible_window=complete result=PASS\n");
#endif
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
