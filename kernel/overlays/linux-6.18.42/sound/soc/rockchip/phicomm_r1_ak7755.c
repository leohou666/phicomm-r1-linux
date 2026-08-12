// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Minimal Phicomm R1 AK7755 machine driver
 *
 * Copyright (C) 2026 Phicomm R1 Linux contributors
 */

#include <linux/capability.h>
#include <linux/delay.h>
#include <linux/fs.h>
#include <linux/ioctl.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/regulator/consumer.h>
#include <linux/sched.h>
#include <linux/workqueue.h>

#include <sound/pcm_params.h>
#include <sound/soc.h>

#define R1_AK7755_RATE		48000
#define R1_AK7755_CHANNELS	2
#define R1_AK7755_BCLK_RATIO	64
#define R1_AK7755_I2S_CLOCK	12288000
#define R1_AUDIO_IOC_ARM_MUTED	_IO('R', 1)
#define R1_AUDIO_IOC_UNMUTE	_IO('R', 2)
#define R1_AUDIO_IOC_SAFE	_IO('R', 3)
#define R1_AUDIO_IOC_KEEPALIVE	_IO('R', 4)
#define R1_AUDIO_IOC_DAC_MUTE	_IO('R', 5)
#define R1_AUDIO_IOC_DAC_UNMUTE	_IO('R', 6)
#define R1_AUDIO_IOC_DAC_ANALOG_OFF _IO('R', 7)
#define R1_AUDIO_IOC_LINEOUT_HIZ	_IO('R', 8)
#define R1_AUDIO_IOC_LINEOUT_MINUS14DB _IO('R', 9)
#define R1_AUDIO_IOC_LINEOUT_MINUS28DB _IO('R', 10)
#define R1_AUDIO_ARM_TIMEOUT_MS	3000
#define R1_AUDIO_LIVE_TIMEOUT_MS	500

int ak7755_component_set_dac_mute(struct snd_soc_component *component,
				   bool mute);
int ak7755_component_set_analog_boundary(struct snd_soc_component *component,
					  unsigned int boundary);
int ak7755_component_set_lineout_volume(struct snd_soc_component *component,
					 unsigned int stage);

struct r1_ak7755_sound {
	struct snd_soc_card card;
	struct snd_soc_dai_link link;
	struct snd_soc_dai_link_component cpu;
	struct snd_soc_dai_link_component codec;
	struct snd_soc_dai_link_component platform;
	struct regulator *amp_enable;
	struct regulator *amp_unmute;
	struct miscdevice safety_misc;
	struct delayed_work safety_timeout;
	struct mutex safety_lock;
	struct snd_soc_component *codec_component;
	atomic_t safety_open;
	pid_t safety_owner_tgid;
	unsigned int pcm_open_count;
	bool playback_open;
	bool playback_diagnostic;
	bool playback_active;
	bool amp_gates_ready;
	bool amp_enabled;
	bool amp_unmuted;
};

static int r1_audio_force_safe_locked(struct r1_ak7755_sound *sound)
{
	int first_error = 0;
	int ret;

	if (!sound->amp_gates_ready)
		return 0;

	if (sound->amp_unmuted) {
		ret = regulator_disable(sound->amp_unmute);
		if (ret) {
			dev_err(sound->card.dev, "failed to assert amplifier mute: %d\n",
				ret);
			first_error = ret;
		} else {
			sound->amp_unmuted = false;
		}
	}

	/* Let MUTE reach the amplifier before asserting SDZ. */
	msleep(10);
	if (sound->amp_enabled) {
		ret = regulator_disable(sound->amp_enable);
		if (ret) {
			dev_err(sound->card.dev,
				"failed to assert amplifier shutdown: %d\n", ret);
			if (!first_error)
				first_error = ret;
		} else {
			sound->amp_enabled = false;
		}
	}
	sound->playback_active = false;

	return first_error;
}

static int r1_audio_start_playback_locked(struct r1_ak7755_sound *sound)
{
	int ret;

	if (!sound->amp_gates_ready)
		return 0;
	if (sound->playback_active)
		return 0;
	if (!sound->playback_open || atomic_read(&sound->safety_open))
		return -EBUSY;

	/* Always begin a new audible interval from the fail-closed state. */
	ret = r1_audio_force_safe_locked(sound);
	if (ret)
		return ret;

	ret = regulator_enable(sound->amp_enable);
	if (ret)
		goto err_safe;
	sound->amp_enabled = true;

	/* Release TPA3118D2 SDZ while MUTE remains asserted. */
	msleep(20);
	ret = regulator_enable(sound->amp_unmute);
	if (ret)
		goto err_safe;
	sound->amp_unmuted = true;
	sound->playback_active = true;
	dev_info(sound->card.dev,
		 "PCM playback active: amplifier settled and unmuted\n");
	return 0;

err_safe:
	r1_audio_force_safe_locked(sound);
	return ret;
}

static void r1_audio_timeout_work(struct work_struct *work)
{
	struct r1_ak7755_sound *sound =
		container_of(to_delayed_work(work), struct r1_ak7755_sound,
			     safety_timeout);

	mutex_lock(&sound->safety_lock);
	if (sound->amp_enabled || sound->amp_unmuted) {
		dev_warn(sound->card.dev,
			 "audible-test keepalive expired; forcing mute+shutdown\n");
		r1_audio_force_safe_locked(sound);
	}
	mutex_unlock(&sound->safety_lock);
}

static int r1_audio_safety_open(struct inode *inode, struct file *file)
{
	struct miscdevice *misc = file->private_data;
	struct r1_ak7755_sound *sound =
		container_of(misc, struct r1_ak7755_sound, safety_misc);
	int ret;

	if (!capable(CAP_SYS_RAWIO))
		return -EPERM;
	if (atomic_cmpxchg(&sound->safety_open, 0, 1))
		return -EBUSY;

	cancel_delayed_work_sync(&sound->safety_timeout);
	mutex_lock(&sound->safety_lock);
	if (sound->pcm_open_count) {
		ret = -EBUSY;
		goto out_unlock;
	}
	ret = r1_audio_force_safe_locked(sound);
	if (!ret)
		sound->safety_owner_tgid = current->tgid;

out_unlock:
	mutex_unlock(&sound->safety_lock);
	if (ret) {
		atomic_set(&sound->safety_open, 0);
		return ret;
	}

	file->private_data = sound;
	return nonseekable_open(inode, file);
}

static long r1_audio_safety_ioctl(struct file *file, unsigned int command,
				  unsigned long argument)
{
	struct r1_ak7755_sound *sound = file->private_data;
	int ret = 0;

	if (command == R1_AUDIO_IOC_SAFE || command == R1_AUDIO_IOC_ARM_MUTED)
		cancel_delayed_work_sync(&sound->safety_timeout);

	mutex_lock(&sound->safety_lock);
	switch (command) {
	case R1_AUDIO_IOC_ARM_MUTED:
		ret = r1_audio_force_safe_locked(sound);
		if (ret)
			break;
		ret = regulator_enable(sound->amp_enable);
		if (ret)
			break;
		sound->amp_enabled = true;
		/* TPA3118D2 remains muted while SDZ is released and settles. */
		msleep(20);
		mod_delayed_work(system_wq, &sound->safety_timeout,
				 msecs_to_jiffies(R1_AUDIO_ARM_TIMEOUT_MS));
		dev_info(sound->card.dev,
			 "audible test armed: amplifier enabled but muted\n");
		break;
	case R1_AUDIO_IOC_UNMUTE:
		if (!sound->amp_enabled || sound->amp_unmuted) {
			ret = -EPERM;
			break;
		}
		ret = regulator_enable(sound->amp_unmute);
		if (ret)
			break;
		sound->amp_unmuted = true;
		mod_delayed_work(system_wq, &sound->safety_timeout,
				 msecs_to_jiffies(R1_AUDIO_LIVE_TIMEOUT_MS));
		dev_warn(sound->card.dev,
			 "audible test UNMUTED; 500 ms fail-safe active\n");
		break;
	case R1_AUDIO_IOC_KEEPALIVE:
		if (!sound->amp_unmuted) {
			ret = -EPERM;
			break;
		}
		mod_delayed_work(system_wq, &sound->safety_timeout,
				 msecs_to_jiffies(R1_AUDIO_LIVE_TIMEOUT_MS));
		break;
	case R1_AUDIO_IOC_DAC_MUTE:
	case R1_AUDIO_IOC_DAC_UNMUTE:
		if (!sound->amp_unmuted || !sound->codec_component) {
			ret = -EPERM;
			break;
		}
		ret = ak7755_component_set_dac_mute(sound->codec_component,
						 command == R1_AUDIO_IOC_DAC_MUTE);
		if (!ret)
			mod_delayed_work(system_wq, &sound->safety_timeout,
					 msecs_to_jiffies(R1_AUDIO_LIVE_TIMEOUT_MS));
		break;
	case R1_AUDIO_IOC_DAC_ANALOG_OFF:
	case R1_AUDIO_IOC_LINEOUT_HIZ:
		if (!sound->amp_unmuted || !sound->codec_component) {
			ret = -EPERM;
			break;
		}
		ret = ak7755_component_set_analog_boundary(
			sound->codec_component,
			command == R1_AUDIO_IOC_DAC_ANALOG_OFF ? 1 : 2);
		if (!ret)
			mod_delayed_work(system_wq, &sound->safety_timeout,
					 msecs_to_jiffies(R1_AUDIO_LIVE_TIMEOUT_MS));
		break;
	case R1_AUDIO_IOC_LINEOUT_MINUS14DB:
	case R1_AUDIO_IOC_LINEOUT_MINUS28DB:
		if (!sound->amp_unmuted || !sound->codec_component) {
			ret = -EPERM;
			break;
		}
		ret = ak7755_component_set_lineout_volume(
			sound->codec_component,
			command == R1_AUDIO_IOC_LINEOUT_MINUS14DB ? 1 : 2);
		if (!ret)
			mod_delayed_work(system_wq, &sound->safety_timeout,
					 msecs_to_jiffies(R1_AUDIO_LIVE_TIMEOUT_MS));
		break;
	case R1_AUDIO_IOC_SAFE:
		ret = r1_audio_force_safe_locked(sound);
		if (!ret)
			dev_info(sound->card.dev,
				 "audible test safe: mute+shutdown asserted\n");
		break;
	default:
		ret = -ENOTTY;
		break;
	}
	mutex_unlock(&sound->safety_lock);

	return ret;
}

static int r1_audio_safety_release(struct inode *inode, struct file *file)
{
	struct r1_ak7755_sound *sound = file->private_data;

	cancel_delayed_work_sync(&sound->safety_timeout);
	mutex_lock(&sound->safety_lock);
	r1_audio_force_safe_locked(sound);
	sound->safety_owner_tgid = 0;
	mutex_unlock(&sound->safety_lock);
	atomic_set(&sound->safety_open, 0);

	return 0;
}

static const struct file_operations r1_audio_safety_fops = {
	.owner = THIS_MODULE,
	.open = r1_audio_safety_open,
	.release = r1_audio_safety_release,
	.unlocked_ioctl = r1_audio_safety_ioctl,
	.llseek = noop_llseek,
};

static int r1_ak7755_set_clock_contract(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_soc_dai *cpu_dai = snd_soc_rtd_to_cpu(rtd, 0);
	int ret;

	ret = snd_soc_dai_set_bclk_ratio(cpu_dai, R1_AK7755_BCLK_RATIO);
	if (ret)
		return dev_err_probe(rtd->dev, ret,
				     "failed to set 64fs bit-clock ratio\n");

	ret = snd_soc_dai_set_sysclk(cpu_dai, 0, R1_AK7755_I2S_CLOCK,
				     SND_SOC_CLOCK_OUT);
	if (ret)
		return dev_err_probe(rtd->dev, ret,
				     "failed to set 12.288 MHz I2S clock\n");

	return 0;
}

static int r1_ak7755_link_init(struct snd_soc_pcm_runtime *rtd)
{
	struct r1_ak7755_sound *sound = snd_soc_card_get_drvdata(rtd->card);
	int ret;

	sound->codec_component = snd_soc_rtd_to_codec(rtd, 0)->component;

	ret = r1_ak7755_set_clock_contract(rtd);
	if (!ret)
		dev_info(rtd->dev,
			 "safe card ready: 48 kHz stereo S16, AK7755 clock provider, 64fs\n");

	return ret;
}

static int r1_ak7755_hw_params(struct snd_pcm_substream *substream,
				struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = snd_soc_substream_to_rtd(substream);

	if (params_rate(params) != R1_AK7755_RATE ||
	    params_channels(params) != R1_AK7755_CHANNELS ||
	    params_format(params) != SNDRV_PCM_FORMAT_S16_LE)
		return -EINVAL;

	return r1_ak7755_set_clock_contract(rtd);
}

static int r1_ak7755_startup(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = snd_soc_substream_to_rtd(substream);
	struct r1_ak7755_sound *sound = snd_soc_card_get_drvdata(rtd->card);
	int ret = 0;

	mutex_lock(&sound->safety_lock);
	if (atomic_read(&sound->safety_open) &&
	    sound->safety_owner_tgid != current->tgid) {
		ret = -EBUSY;
		goto out_unlock;
	}
	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		if (sound->playback_open) {
			ret = -EBUSY;
			goto out_unlock;
		}
		sound->playback_open = true;
		sound->playback_diagnostic = atomic_read(&sound->safety_open);
	}
	sound->pcm_open_count++;

out_unlock:
	mutex_unlock(&sound->safety_lock);
	return ret;
}

static int r1_ak7755_prepare(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = snd_soc_substream_to_rtd(substream);
	struct r1_ak7755_sound *sound = snd_soc_card_get_drvdata(rtd->card);
	int ret = 0;

	if (substream->stream != SNDRV_PCM_STREAM_PLAYBACK)
		return 0;

	mutex_lock(&sound->safety_lock);
	if (sound->playback_diagnostic)
		ret = 0;
	else if (atomic_read(&sound->safety_open))
		ret = -EBUSY;
	else
		ret = r1_audio_force_safe_locked(sound);
	mutex_unlock(&sound->safety_lock);
	return ret;
}

static int r1_ak7755_trigger(struct snd_pcm_substream *substream, int cmd)
{
	struct snd_soc_pcm_runtime *rtd = snd_soc_substream_to_rtd(substream);
	struct r1_ak7755_sound *sound = snd_soc_card_get_drvdata(rtd->card);
	int ret = 0;

	if (substream->stream != SNDRV_PCM_STREAM_PLAYBACK)
		return 0;

	mutex_lock(&sound->safety_lock);
	if (sound->playback_diagnostic)
		goto out_unlock;
	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		ret = r1_audio_start_playback_locked(sound);
		break;
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		ret = r1_audio_force_safe_locked(sound);
		if (!ret)
			dev_info(sound->card.dev,
				 "PCM playback stopped: mute+shutdown asserted\n");
		break;
	default:
		ret = -EINVAL;
		break;
	}

out_unlock:
	mutex_unlock(&sound->safety_lock);
	return ret;
}

static int r1_ak7755_hw_free(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = snd_soc_substream_to_rtd(substream);
	struct r1_ak7755_sound *sound = snd_soc_card_get_drvdata(rtd->card);
	int ret = 0;

	if (substream->stream != SNDRV_PCM_STREAM_PLAYBACK)
		return 0;
	mutex_lock(&sound->safety_lock);
	if (!sound->playback_diagnostic)
		ret = r1_audio_force_safe_locked(sound);
	mutex_unlock(&sound->safety_lock);
	return ret;
}

static void r1_ak7755_pcm_shutdown(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = snd_soc_substream_to_rtd(substream);
	struct r1_ak7755_sound *sound = snd_soc_card_get_drvdata(rtd->card);

	mutex_lock(&sound->safety_lock);
	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		if (!sound->playback_diagnostic)
			r1_audio_force_safe_locked(sound);
		sound->playback_open = false;
		sound->playback_diagnostic = false;
	}
	if (WARN_ON(!sound->pcm_open_count))
		goto out_unlock;
	sound->pcm_open_count--;

out_unlock:
	mutex_unlock(&sound->safety_lock);
}

static const struct snd_soc_ops r1_ak7755_ops = {
	.startup = r1_ak7755_startup,
	.shutdown = r1_ak7755_pcm_shutdown,
	.hw_params = r1_ak7755_hw_params,
	.hw_free = r1_ak7755_hw_free,
	.prepare = r1_ak7755_prepare,
	.trigger = r1_ak7755_trigger,
};

static void r1_ak7755_put_of_nodes(void *data)
{
	struct r1_ak7755_sound *sound = data;

	of_node_put(sound->cpu.of_node);
	of_node_put(sound->codec.of_node);
}

static int r1_ak7755_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct r1_ak7755_sound *sound;
	int ret;

	sound = devm_kzalloc(dev, sizeof(*sound), GFP_KERNEL);
	if (!sound)
		return -ENOMEM;

	sound->cpu.of_node = of_parse_phandle(dev->of_node,
					      "phicomm,cpu-dai", 0);
	if (!sound->cpu.of_node)
		return dev_err_probe(dev, -EINVAL,
				     "missing phicomm,cpu-dai\n");

	sound->codec.of_node = of_parse_phandle(dev->of_node,
						"phicomm,audio-codec", 0);
	if (!sound->codec.of_node) {
		of_node_put(sound->cpu.of_node);
		return dev_err_probe(dev, -EINVAL,
				     "missing phicomm,audio-codec\n");
	}

	ret = devm_add_action_or_reset(dev, r1_ak7755_put_of_nodes, sound);
	if (ret)
		return ret;

	sound->codec.dai_name = "ak7755-AIF1";
	sound->platform.of_node = sound->cpu.of_node;

	sound->link.name = "AK7755-I2S2";
	sound->link.stream_name = "AK7755 PCM";
	sound->link.cpus = &sound->cpu;
	sound->link.num_cpus = 1;
	sound->link.codecs = &sound->codec;
	sound->link.num_codecs = 1;
	sound->link.platforms = &sound->platform;
	sound->link.num_platforms = 1;
	sound->link.init = r1_ak7755_link_init;
	sound->link.ops = &r1_ak7755_ops;
	/* trigger() uses regulators and msleep(), so ALSA must call it sleepably. */
	sound->link.nonatomic = 1;
	sound->link.dai_fmt = SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF |
				     SND_SOC_DAIFMT_CBP_CFP;

	sound->card.name = "RK_AK7755";
	sound->card.owner = THIS_MODULE;
	sound->card.dev = dev;
	sound->card.dai_link = &sound->link;
	sound->card.num_links = 1;
	platform_set_drvdata(pdev, sound);
	snd_soc_card_set_drvdata(&sound->card, sound);
	mutex_init(&sound->safety_lock);
	atomic_set(&sound->safety_open, 0);
	INIT_DELAYED_WORK(&sound->safety_timeout, r1_audio_timeout_work);

	sound->amp_enable = devm_regulator_get_optional(dev, "amp-enable");
	if (IS_ERR(sound->amp_enable)) {
		if (PTR_ERR(sound->amp_enable) == -ENODEV) {
			sound->amp_enable = NULL;
			sound->amp_unmute = NULL;
		} else {
			return dev_err_probe(dev, PTR_ERR(sound->amp_enable),
					     "failed to get amplifier-enable gate\n");
		}
	} else {
		sound->amp_unmute = devm_regulator_get(dev, "amp-unmute");
		if (IS_ERR(sound->amp_unmute))
			return dev_err_probe(dev, PTR_ERR(sound->amp_unmute),
					     "failed to get amplifier-unmute gate\n");
		sound->amp_gates_ready = true;
	}

	ret = devm_snd_soc_register_card(dev, &sound->card);
	if (ret)
		return dev_err_probe(dev, ret, "failed to register sound card\n");

	if (!sound->amp_gates_ready) {
		dev_warn(dev,
			 "amplifier gates absent; automatic playback control disabled\n");
		return 0;
	}

	sound->safety_misc.minor = MISC_DYNAMIC_MINOR;
	sound->safety_misc.name = "r1-audio-safety";
	sound->safety_misc.fops = &r1_audio_safety_fops;
	sound->safety_misc.parent = dev;
	sound->safety_misc.mode = 0600;

	ret = misc_register(&sound->safety_misc);
	if (ret)
		return dev_err_probe(dev, ret,
				     "failed to register audible-test safety gate\n");

	dev_info(dev,
		 "automatic PCM amplifier control and exclusive audible-test gate ready; default mute+shutdown\n");

	return 0;
}

static void r1_ak7755_remove(struct platform_device *pdev)
{
	struct r1_ak7755_sound *sound = platform_get_drvdata(pdev);

	if (!sound || !sound->safety_misc.this_device)
		return;

	misc_deregister(&sound->safety_misc);
	cancel_delayed_work_sync(&sound->safety_timeout);
	mutex_lock(&sound->safety_lock);
	r1_audio_force_safe_locked(sound);
	mutex_unlock(&sound->safety_lock);
}

static void r1_ak7755_shutdown(struct platform_device *pdev)
{
	struct r1_ak7755_sound *sound = platform_get_drvdata(pdev);

	if (!sound || !sound->safety_misc.this_device)
		return;

	cancel_delayed_work_sync(&sound->safety_timeout);
	mutex_lock(&sound->safety_lock);
	r1_audio_force_safe_locked(sound);
	mutex_unlock(&sound->safety_lock);
}

static const struct of_device_id r1_ak7755_of_match[] = {
	{ .compatible = "phicomm,r1-ak7755-sound" },
	{ }
};
MODULE_DEVICE_TABLE(of, r1_ak7755_of_match);

static struct platform_driver r1_ak7755_driver = {
	.probe = r1_ak7755_probe,
	.remove = r1_ak7755_remove,
	.shutdown = r1_ak7755_shutdown,
	.driver = {
		.name = "phicomm-r1-ak7755",
		.of_match_table = r1_ak7755_of_match,
	},
};
module_platform_driver(r1_ak7755_driver);

MODULE_DESCRIPTION("Phicomm R1 AK7755 safe machine driver");
MODULE_AUTHOR("Phicomm R1 Linux contributors");
MODULE_LICENSE("GPL");
