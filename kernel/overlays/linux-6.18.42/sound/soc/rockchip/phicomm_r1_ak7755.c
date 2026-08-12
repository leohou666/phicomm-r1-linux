// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Minimal Phicomm R1 AK7755 machine driver
 *
 * Copyright (C) 2026 Phicomm R1 Linux contributors
 */

#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>

#include <sound/pcm_params.h>
#include <sound/soc.h>

#define R1_AK7755_RATE		48000
#define R1_AK7755_CHANNELS	2
#define R1_AK7755_BCLK_RATIO	32
#define R1_AK7755_I2S_CLOCK	12288000

struct r1_ak7755_sound {
	struct snd_soc_card card;
	struct snd_soc_dai_link link;
	struct snd_soc_dai_link_component cpu;
	struct snd_soc_dai_link_component codec;
	struct snd_soc_dai_link_component platform;
};

static int r1_ak7755_set_clock_contract(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_soc_dai *cpu_dai = snd_soc_rtd_to_cpu(rtd, 0);
	int ret;

	ret = snd_soc_dai_set_bclk_ratio(cpu_dai, R1_AK7755_BCLK_RATIO);
	if (ret)
		return dev_err_probe(rtd->dev, ret,
				     "failed to set 32fs bit-clock ratio\n");

	ret = snd_soc_dai_set_sysclk(cpu_dai, 0, R1_AK7755_I2S_CLOCK,
				     SND_SOC_CLOCK_OUT);
	if (ret)
		return dev_err_probe(rtd->dev, ret,
				     "failed to set 12.288 MHz I2S clock\n");

	return 0;
}

static int r1_ak7755_link_init(struct snd_soc_pcm_runtime *rtd)
{
	int ret;

	ret = r1_ak7755_set_clock_contract(rtd);
	if (!ret)
		dev_info(rtd->dev,
			 "safe card ready: 48 kHz stereo S16, CPU clock provider, 32fs\n");

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

static const struct snd_soc_ops r1_ak7755_ops = {
	.hw_params = r1_ak7755_hw_params,
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
	sound->link.dai_fmt = SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF |
				     SND_SOC_DAIFMT_CBC_CFC;

	sound->card.name = "RK_AK7755";
	sound->card.owner = THIS_MODULE;
	sound->card.dev = dev;
	sound->card.dai_link = &sound->link;
	sound->card.num_links = 1;

	ret = devm_snd_soc_register_card(dev, &sound->card);
	if (ret)
		return dev_err_probe(dev, ret, "failed to register sound card\n");

	return 0;
}

static const struct of_device_id r1_ak7755_of_match[] = {
	{ .compatible = "phicomm,r1-ak7755-sound" },
	{ }
};
MODULE_DEVICE_TABLE(of, r1_ak7755_of_match);

static struct platform_driver r1_ak7755_driver = {
	.probe = r1_ak7755_probe,
	.driver = {
		.name = "phicomm-r1-ak7755",
		.of_match_table = r1_ak7755_of_match,
	},
};
module_platform_driver(r1_ak7755_driver);

MODULE_DESCRIPTION("Phicomm R1 AK7755 safe machine driver");
MODULE_AUTHOR("Phicomm R1 Linux contributors");
MODULE_LICENSE("GPL");
