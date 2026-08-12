// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Minimal AKM AK7755 audio DSP component
 *
 * Copyright (C) 2014-2016 Asahi Kasei Microdevices Corporation
 * Copyright (C) 2026 Phicomm R1 Linux contributors
 *
 * The RAM download and initial serial-port programming are derived from
 * AKM's GPL-licensed Linux 3.10 reference driver.  The first PCM milestone
 * is deliberately narrow and fail-closed: 48 kHz, stereo, S16, codec clock
 * consumer, 32fs.  The DSP remains stopped and this driver does not control
 * the external amplifier.
 */

#include <linux/delay.h>
#include <linux/firmware.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/property.h>
#include <linux/regulator/consumer.h>
#include <linux/slab.h>

#include <sound/soc.h>

#define AK7755_CMD_DEVICE_ID		0x60
#define AK7755_DEVICE_ID			0x55
#define AK7755_CMD_CRC_RESULT		0x72

#define AK7755_REG_D0_FUNCTION		0xd0
#define AK7755_REG_CF_RESET_POWER	0xcf
#define AK7755_REG_C0_CLOCK_SETTING1	0xc0
#define AK7755_REG_C1_CLOCK_SETTING2	0xc1
#define AK7755_REG_C2_SERIAL_FORMAT	0xc2
#define AK7755_REG_C3_DSP_IO		0xc3
#define AK7755_REG_C6_DAC_FORMAT		0xc6
#define AK7755_REG_C7_DSP_OUTPUT		0xc7
#define AK7755_REG_CA_CLOCK_OUTPUT	0xca
#define AK7755_D0_CRCE			BIT(6)
#define AK7755_CF_DLRDY			BIT(0)

#define AK7755_C0_FS_MASK		GENMASK(2, 0)
#define AK7755_C0_FS_48KHZ		0x05
#define AK7755_C0_CLOCK_MODE_MASK	GENMASK(5, 4)
#define AK7755_C0_SLAVE_BICK		GENMASK(5, 4)
#define AK7755_C1_BICK_FS_MASK		GENMASK(5, 4)
#define AK7755_C1_BICK_32FS		BIT(5)
#define AK7755_C2_LRIF_MASK		GENMASK(5, 4)
#define AK7755_C2_LRIF_I2S		BIT(4)
#define AK7755_CA_BICK_LRCK_OUTPUT	GENMASK(6, 5)

#define AK7755_PRAM_WRITE		0xb8
#define AK7755_CRAM_WRITE		0xb4
#define AK7755_PRAM_MAX_BYTES		20483
#define AK7755_CRAM_MAX_BYTES		6147

struct ak7755_ram_image {
	const char *property;
	const char *default_name;
	const char *label;
	u8 command;
	size_t max_size;
};

static const struct ak7755_ram_image ak7755_ram_images[] = {
	{
		.property = "akm,pram-firmware",
		.default_name = "ak7755_pram_data2.bin",
		.label = "PRAM",
		.command = AK7755_PRAM_WRITE,
		.max_size = AK7755_PRAM_MAX_BYTES,
	},
	{
		.property = "akm,cram-firmware",
		.default_name = "ak7755_cram_data2.bin",
		.label = "CRAM",
		.command = AK7755_CRAM_WRITE,
		.max_size = AK7755_CRAM_MAX_BYTES,
	},
};

struct ak7755_priv {
	struct i2c_client *client;
	struct gpio_desc *reset_gpio;
	struct mutex lock;
};

/* AK7755 reads use a command byte, followed by a repeated-start read. */
static int ak7755_command_read(struct ak7755_priv *ak7755, u8 command,
				void *data, size_t len)
{
	struct i2c_msg messages[] = {
		{
			.addr = ak7755->client->addr,
			.len = 1,
			.buf = &command,
		},
		{
			.addr = ak7755->client->addr,
			.flags = I2C_M_RD,
			.len = len,
			.buf = data,
		},
	};
	int ret;

	ret = i2c_transfer(ak7755->client->adapter, messages,
			   ARRAY_SIZE(messages));
	if (ret == ARRAY_SIZE(messages))
		return 0;
	if (ret >= 0)
		return -EIO;

	return ret;
}

static int ak7755_register_read(struct ak7755_priv *ak7755, u8 reg, u8 *value)
{
	return ak7755_command_read(ak7755, reg & 0x7f, value, 1);
}

static int ak7755_register_write(struct ak7755_priv *ak7755, u8 reg, u8 value)
{
	u8 data[] = { reg, value };
	int ret;

	ret = i2c_master_send(ak7755->client, data, sizeof(data));
	if (ret == sizeof(data))
		return 0;
	if (ret >= 0)
		return -EIO;

	return ret;
}

static int ak7755_update_bits(struct ak7755_priv *ak7755, u8 reg,
			       u8 mask, u8 value)
{
	u8 old_value;
	int ret;

	ret = ak7755_register_read(ak7755, reg, &old_value);
	if (ret)
		return ret;

	value = (old_value & ~mask) | (value & mask);
	if (value == old_value)
		return 0;

	return ak7755_register_write(ak7755, reg, value);
}

static u16 ak7755_crc16(const u8 *data, size_t len)
{
	u16 crc = 0;
	size_t i;
	int bit;

	for (i = 0; i < len; i++) {
		crc ^= (u16)data[i] << 8;
		for (bit = 0; bit < 8; bit++)
			crc = (crc << 1) ^ ((crc & BIT(15)) ? 0x1021 : 0);
	}

	return crc;
}

static int ak7755_read_crc(struct ak7755_priv *ak7755, u16 *crc)
{
	u8 bytes[2];
	int ret;

	ret = ak7755_command_read(ak7755, AK7755_CMD_CRC_RESULT,
				  bytes, sizeof(bytes));
	if (ret)
		return ret;

	*crc = ((u16)bytes[0] << 8) | bytes[1];
	return 0;
}

static int ak7755_send_firmware(struct ak7755_priv *ak7755,
				 const struct firmware *firmware)
{
	int ret;

	ret = i2c_master_send(ak7755->client, firmware->data, firmware->size);
	if (ret == (int)firmware->size)
		return 0;
	if (ret >= 0)
		return -EIO;

	return ret;
}

static int ak7755_download_image(struct ak7755_priv *ak7755,
				  const struct ak7755_ram_image *image)
{
	const struct firmware *firmware;
	const char *firmware_name;
	u16 expected_crc;
	u16 device_crc;
	u8 old_cf;
	int ret;

	ret = device_property_read_string(&ak7755->client->dev,
					  image->property, &firmware_name);
	if (ret == -EINVAL || ret == -ENODATA || ret == -ENOENT) {
		firmware_name = image->default_name;
	} else if (ret) {
		return dev_err_probe(&ak7755->client->dev, ret,
				     "failed to read %s\n", image->property);
	}

	ret = request_firmware(&firmware, firmware_name, &ak7755->client->dev);
	if (ret)
		return dev_err_probe(&ak7755->client->dev, ret,
				     "failed to request %s firmware %s\n",
				     image->label, firmware_name);

	if (firmware->size < 4 || firmware->size > image->max_size) {
		dev_err(&ak7755->client->dev,
			"%s firmware size %zu is outside 4..%zu\n",
			image->label, firmware->size, image->max_size);
		ret = -EINVAL;
		goto out_release;
	}

	if (firmware->data[0] != image->command) {
		dev_err(&ak7755->client->dev,
			"%s firmware command %#02x, expected %#02x\n",
			image->label, firmware->data[0], image->command);
		ret = -EINVAL;
		goto out_release;
	}

	expected_crc = ak7755_crc16(firmware->data, firmware->size);

	mutex_lock(&ak7755->lock);
	ret = ak7755_register_read(ak7755, AK7755_REG_CF_RESET_POWER, &old_cf);
	if (ret)
		goto out_unlock;

	ret = ak7755_register_write(ak7755, AK7755_REG_CF_RESET_POWER,
				     old_cf | AK7755_CF_DLRDY);
	if (ret)
		goto out_unlock;

	usleep_range(1000, 2000);
	ret = ak7755_send_firmware(ak7755, firmware);
	if (ret)
		goto out_restore;

	ret = ak7755_read_crc(ak7755, &device_crc);
	if (ret)
		goto out_restore;

	if (device_crc != expected_crc) {
		dev_err(&ak7755->client->dev,
			"%s CRC mismatch: calculated %04x, device %04x\n",
			image->label, expected_crc, device_crc);
		ret = -EBADMSG;
		goto out_restore;
	}

	dev_info(&ak7755->client->dev,
		 "%s firmware %s: %zu bytes, CRC %04x verified\n",
		 image->label, firmware_name, firmware->size, device_crc);

out_restore:
	/* Keep the DSP out of run state; this milestone must not produce audio. */
	if (ak7755_register_write(ak7755, AK7755_REG_CF_RESET_POWER, old_cf) && !ret)
		ret = -EIO;
out_unlock:
	mutex_unlock(&ak7755->lock);
out_release:
	release_firmware(firmware);
	return ret;
}

static const struct snd_soc_component_driver ak7755_component_driver = {
	.name = "ak7755",
};

static int ak7755_set_dai_fmt(struct snd_soc_dai *dai, unsigned int fmt)
{
	struct ak7755_priv *ak7755 = snd_soc_component_get_drvdata(dai->component);
	int ret;

	if ((fmt & SND_SOC_DAIFMT_FORMAT_MASK) != SND_SOC_DAIFMT_I2S ||
	    (fmt & SND_SOC_DAIFMT_INV_MASK) != SND_SOC_DAIFMT_NB_NF ||
	    (fmt & SND_SOC_DAIFMT_CLOCK_PROVIDER_MASK) != SND_SOC_DAIFMT_BC_FC)
		return -EINVAL;

	mutex_lock(&ak7755->lock);

	/* R1 has no codec MCLK pin: derive the codec clock from CPU BICK. */
	ret = ak7755_update_bits(ak7755, AK7755_REG_C0_CLOCK_SETTING1,
				 AK7755_C0_CLOCK_MODE_MASK | AK7755_C0_FS_MASK,
				 AK7755_C0_SLAVE_BICK | AK7755_C0_FS_48KHZ);
	if (ret)
		goto out_unlock;

	ret = ak7755_update_bits(ak7755, AK7755_REG_CA_CLOCK_OUTPUT,
				 AK7755_CA_BICK_LRCK_OUTPUT, 0);
	if (ret)
		goto out_unlock;

	ret = ak7755_update_bits(ak7755, AK7755_REG_C1_CLOCK_SETTING2,
				 AK7755_C1_BICK_FS_MASK, AK7755_C1_BICK_32FS);
	if (ret)
		goto out_unlock;

	ret = ak7755_update_bits(ak7755, AK7755_REG_C2_SERIAL_FORMAT,
				 AK7755_C2_LRIF_MASK, AK7755_C2_LRIF_I2S);
	if (ret)
		goto out_unlock;

	/* Match AKM's GPL I2S/32fs serial-input and serial-output fields. */
	ret = ak7755_update_bits(ak7755, AK7755_REG_C3_DSP_IO, 0xf0, 0xf0);
	if (ret)
		goto out_unlock;

	ret = ak7755_update_bits(ak7755, AK7755_REG_C6_DAC_FORMAT, 0x37, 0x33);
	if (ret)
		goto out_unlock;

	ret = ak7755_register_write(ak7755, AK7755_REG_C7_DSP_OUTPUT, 0xf3);

out_unlock:
	mutex_unlock(&ak7755->lock);
	if (!ret)
		dev_info_once(&ak7755->client->dev,
			      "DAI prepared: I2S 48 kHz stereo S16, codec slave, 32fs; DSP stopped\n");

	return ret;
}

static const struct snd_soc_dai_ops ak7755_dai_ops = {
	.set_fmt = ak7755_set_dai_fmt,
};

static struct snd_soc_dai_driver ak7755_dai = {
	.name = "ak7755-AIF1",
	.playback = {
		.stream_name = "Playback",
		.channels_min = 2,
		.channels_max = 2,
		.rates = SNDRV_PCM_RATE_48000,
		.formats = SNDRV_PCM_FMTBIT_S16_LE,
	},
	.capture = {
		.stream_name = "Capture",
		.channels_min = 2,
		.channels_max = 2,
		.rates = SNDRV_PCM_RATE_48000,
		.formats = SNDRV_PCM_FMTBIT_S16_LE,
	},
	.ops = &ak7755_dai_ops,
};

static int ak7755_i2c_probe(struct i2c_client *client)
{
	struct ak7755_priv *ak7755;
	u8 device_id;
	unsigned int i;
	int ret;

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C))
		return -EOPNOTSUPP;

	ak7755 = devm_kzalloc(&client->dev, sizeof(*ak7755), GFP_KERNEL);
	if (!ak7755)
		return -ENOMEM;

	ak7755->client = client;
	mutex_init(&ak7755->lock);
	i2c_set_clientdata(client, ak7755);

	ret = devm_regulator_get_enable(&client->dev, "safe");
	if (ret)
		return dev_err_probe(&client->dev, ret,
				     "failed to establish amplifier-safe state\n");

	ak7755->reset_gpio = devm_gpiod_get(&client->dev, "reset",
					    GPIOD_OUT_HIGH);
	if (IS_ERR(ak7755->reset_gpio))
		return dev_err_probe(&client->dev, PTR_ERR(ak7755->reset_gpio),
				     "failed to get reset GPIO\n");

	usleep_range(2000, 3000);
	gpiod_set_value_cansleep(ak7755->reset_gpio, 0);
	usleep_range(2000, 3000);

	ret = ak7755_command_read(ak7755, AK7755_CMD_DEVICE_ID,
				  &device_id, sizeof(device_id));
	if (ret)
		goto err_assert_reset;
	if (device_id != AK7755_DEVICE_ID) {
		dev_err(&client->dev, "unexpected device ID %#02x\n", device_id);
		ret = -ENODEV;
		goto err_assert_reset;
	}

	ret = ak7755_update_bits(ak7755, AK7755_REG_D0_FUNCTION,
				 AK7755_D0_CRCE, AK7755_D0_CRCE);
	if (ret)
		goto err_assert_reset;

	for (i = 0; i < ARRAY_SIZE(ak7755_ram_images); i++) {
		ret = ak7755_download_image(ak7755, &ak7755_ram_images[i]);
		if (ret)
			goto err_assert_reset;
	}

	ret = devm_snd_soc_register_component(&client->dev,
					      &ak7755_component_driver,
					      &ak7755_dai, 1);
	if (ret)
		goto err_assert_reset;

	dev_info(&client->dev,
		 "AK7755EN ID 0x55; firmware verified, DSP intentionally stopped\n");
	return 0;

err_assert_reset:
	gpiod_set_value_cansleep(ak7755->reset_gpio, 1);
	return dev_err_probe(&client->dev, ret,
			     "safe firmware verification failed\n");
}

static void ak7755_i2c_remove(struct i2c_client *client)
{
	struct ak7755_priv *ak7755 = i2c_get_clientdata(client);

	gpiod_set_value_cansleep(ak7755->reset_gpio, 1);
}

static const struct of_device_id ak7755_of_match[] = {
	{ .compatible = "akm,ak7755" },
	{ }
};
MODULE_DEVICE_TABLE(of, ak7755_of_match);

static const struct i2c_device_id ak7755_i2c_ids[] = {
	{ "ak7755" },
	{ }
};
MODULE_DEVICE_TABLE(i2c, ak7755_i2c_ids);

static struct i2c_driver ak7755_i2c_driver = {
	.driver = {
		.name = "ak7755",
		.of_match_table = ak7755_of_match,
	},
	.probe = ak7755_i2c_probe,
	.remove = ak7755_i2c_remove,
	.id_table = ak7755_i2c_ids,
};
module_i2c_driver(ak7755_i2c_driver);

MODULE_DESCRIPTION("AKM AK7755 audio DSP safe firmware and PCM component");
MODULE_AUTHOR("Phicomm R1 Linux contributors");
MODULE_LICENSE("GPL");
