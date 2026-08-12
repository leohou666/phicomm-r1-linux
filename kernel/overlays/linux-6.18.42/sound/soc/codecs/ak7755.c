// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Minimal AKM AK7755 audio DSP component
 *
 * Copyright (C) 2014-2016 Asahi Kasei Microdevices Corporation
 * Copyright (C) 2026 Phicomm R1 Linux contributors
 *
 * The RAM download and initial serial-port programming are derived from
 * AKM's GPL-licensed Linux 3.10 reference driver.  The first PCM milestone
 * is deliberately narrow and fail-closed: 48 kHz, stereo and S16.  The R1
 * factory media-speaker route is reproduced from device evidence: AK7755 is
 * the BICK/LRCK provider, data2 PRAM/CRAM/OFREG are CRC-verified, and DSP
 * DOUT4 feeds both line outputs.  This driver does not control the external
 * amplifier.
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
#define AK7755_REG_C4_DATA_RAM		0xc4
#define AK7755_REG_C6_DAC_FORMAT		0xc6
#define AK7755_REG_C7_DSP_OUTPUT		0xc7
#define AK7755_REG_C8_DAC_INPUT		0xc8
#define AK7755_REG_C9_ANALOG_IO		0xc9
#define AK7755_REG_CA_CLOCK_OUTPUT	0xca
#define AK7755_REG_CD_STATUS_DOWNLOAD	0xcd
#define AK7755_REG_CE_POWER_MANAGEMENT	0xce
#define AK7755_REG_D3_LINEIN_OUT3_VOLUME	0xd3
#define AK7755_REG_D4_LINEOUT_VOLUME	0xd4
#define AK7755_REG_DA_MUTE_CONTROL	0xda
#define AK7755_REG_E6_REQUIRED_ONE	0xe6
#define AK7755_REG_EA_REQUIRED_ONE	0xea
#define AK7755_D0_CRCE			BIT(6)
#define AK7755_CF_DLRDY			BIT(0)
#define AK7755_CF_DSPRESETN		BIT(2)
#define AK7755_CF_CRESETN		BIT(3)
#define AK7755_CF_RUN_MASK		(AK7755_CF_CRESETN | \
					 AK7755_CF_DSPRESETN)

#define AK7755_C0_FS_MASK		GENMASK(2, 0)
#define AK7755_C0_FS_48KHZ		0x05
#define AK7755_C0_ANALOG_INPUT_ENABLE	BIT(3)
#define AK7755_C0_CLOCK_MODE_MASK	GENMASK(5, 4)
#define AK7755_C0_SLAVE_BICK		GENMASK(5, 4)
#define AK7755_C1_BICK_FS_MASK		GENMASK(5, 4)
#define AK7755_C1_BICK_32FS		BIT(5)
#define AK7755_C1_CKRESETN		BIT(0)
#define AK7755_C2_LRIF_MASK		GENMASK(5, 4)
#define AK7755_C2_LRIF_I2S		BIT(4)
#define AK7755_CA_BICK_LRCK_OUTPUT	GENMASK(6, 5)
#define AK7755_C6_DAC_INPUT_FORMAT_MASK	0x37
#define AK7755_C6_DAC_INPUT_I2S		0x33
#define AK7755_C8_DAC_INPUT_MASK		GENMASK(7, 6)
#define AK7755_C8_DAC_INPUT_SDIN1	GENMASK(7, 6)
#define AK7755_C9_ALL_MIXERS_OFF		0x00
#define AK7755_CE_DAC_LEFT		BIT(0)
#define AK7755_CE_DAC_RIGHT		BIT(1)
#define AK7755_CE_LINEOUT1		BIT(2)
#define AK7755_CE_DIRECT_OUTPUT_MASK	GENMASK(2, 0)
#define AK7755_CE_DIRECT_OUTPUT_ON	GENMASK(2, 0)
#define AK7755_CF_ADC2_RIGHT		BIT(1)
#define AK7755_CF_LINEIN			BIT(5)
#define AK7755_D4_LINEOUT1_VOLUME_MASK	GENMASK(3, 0)
#define AK7755_D4_LINEOUT1_0DB		GENMASK(3, 0)
#define AK7755_D4_LINEOUT1_MINUS14DB	0x08
#define AK7755_D4_LINEOUT1_MINUS28DB	0x01
#define AK7755_DA_DAC_MUTE		BIT(5)
#define AK7755_DA_REQUIRED_ONE		BIT(4)
#define AK7755_CD_REQUIRED_ONE		BIT(6)
#define AK7755_E6_REQUIRED_ONE_VALUE	BIT(0)
#define AK7755_EA_REQUIRED_ONE_VALUE	BIT(7)
#define AK7755_ANALOG_BOUNDARY_DAC_OFF	1
#define AK7755_ANALOG_BOUNDARY_LINEOUT_HIZ 2
#define AK7755_LINEOUT_VOLUME_MINUS14DB	1
#define AK7755_LINEOUT_VOLUME_MINUS28DB	2

#define AK7755_PRAM_WRITE		0xb8
#define AK7755_CRAM_WRITE		0xb4
#define AK7755_OFREG_WRITE		0xb2
#define AK7755_PRAM_MAX_BYTES		20483
#define AK7755_CRAM_MAX_BYTES		6147
#define AK7755_OFREG_MAX_BYTES		99

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
	{
		.property = "akm,ofreg-firmware",
		.default_name = "ak7755_ofreg_data2.bin",
		.label = "OFREG",
		.command = AK7755_OFREG_WRITE,
		.max_size = AK7755_OFREG_MAX_BYTES,
	},
};

struct ak7755_priv {
	struct i2c_client *client;
	struct gpio_desc *reset_gpio;
	struct mutex lock;
	unsigned int open_streams;
	unsigned int analog_boundary;
	unsigned int lineout_volume_stage;
	bool direct_output_running;
};

int ak7755_component_set_dac_mute(struct snd_soc_component *component,
				   bool mute);
int ak7755_component_set_analog_boundary(struct snd_soc_component *component,
					  unsigned int boundary);
int ak7755_component_set_lineout_volume(struct snd_soc_component *component,
					 unsigned int stage);

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

/*
 * AK7755EN datasheet 014006643-E-01 requires CD.D6, DA.D4, E6.D0 and
 * EA.D7 to be set while CRESETN and DSPRESETN are both zero.  These bits
 * latch until the next PDN cycle.  The recovered R1 factory kernel's
 * ak7755_init_reg() writes the same four-bit reset contract.
 */
static int ak7755_apply_system_reset_contract(struct ak7755_priv *ak7755)
{
	u8 cd, da, e6, ea;
	int ret;

	/* Reserved fields are required to be zero; CD.D7 is read-only STO. */
	ret = ak7755_register_write(ak7755, AK7755_REG_CD_STATUS_DOWNLOAD,
				    AK7755_CD_REQUIRED_ONE);
	if (ret)
		return ret;
	ret = ak7755_update_bits(ak7755, AK7755_REG_DA_MUTE_CONTROL,
				 AK7755_DA_REQUIRED_ONE,
				 AK7755_DA_REQUIRED_ONE);
	if (ret)
		return ret;
	ret = ak7755_register_write(ak7755, AK7755_REG_E6_REQUIRED_ONE,
				    AK7755_E6_REQUIRED_ONE_VALUE);
	if (ret)
		return ret;
	ret = ak7755_register_write(ak7755, AK7755_REG_EA_REQUIRED_ONE,
				    AK7755_EA_REQUIRED_ONE_VALUE);
	if (ret)
		return ret;

	ret = ak7755_register_read(ak7755, AK7755_REG_CD_STATUS_DOWNLOAD, &cd);
	if (ret)
		return ret;
	ret = ak7755_register_read(ak7755, AK7755_REG_DA_MUTE_CONTROL, &da);
	if (ret)
		return ret;
	ret = ak7755_register_read(ak7755, AK7755_REG_E6_REQUIRED_ONE, &e6);
	if (ret)
		return ret;
	ret = ak7755_register_read(ak7755, AK7755_REG_EA_REQUIRED_ONE, &ea);
	if (ret)
		return ret;

	if (!(cd & AK7755_CD_REQUIRED_ONE) ||
	    !(da & AK7755_DA_REQUIRED_ONE) ||
	    e6 != AK7755_E6_REQUIRED_ONE_VALUE ||
	    ea != AK7755_EA_REQUIRED_ONE_VALUE) {
		dev_err(&ak7755->client->dev,
			"system reset contract mismatch: CD=%#02x DA=%#02x E6=%#02x EA=%#02x\n",
			cd, da, e6, ea);
		return -EIO;
	}

	dev_info(&ak7755->client->dev,
		 "system reset contract verified: CD=%#02x DA=%#02x E6=%#02x EA=%#02x\n",
		 cd, da, e6, ea);
	return 0;
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

static int ak7755_log_route_snapshot_locked(struct ak7755_priv *ak7755)
{
	u8 c0, c1, c2, c3, c6, c7, c8, ce, d4, da, cf;
	int ret;

	ret = ak7755_register_read(ak7755, AK7755_REG_C0_CLOCK_SETTING1, &c0);
	if (ret)
		return ret;
	ret = ak7755_register_read(ak7755, AK7755_REG_C1_CLOCK_SETTING2, &c1);
	if (ret)
		return ret;
	ret = ak7755_register_read(ak7755, AK7755_REG_C2_SERIAL_FORMAT, &c2);
	if (ret)
		return ret;
	ret = ak7755_register_read(ak7755, AK7755_REG_C3_DSP_IO, &c3);
	if (ret)
		return ret;
	ret = ak7755_register_read(ak7755, AK7755_REG_C6_DAC_FORMAT, &c6);
	if (ret)
		return ret;
	ret = ak7755_register_read(ak7755, AK7755_REG_C7_DSP_OUTPUT, &c7);
	if (ret)
		return ret;
	ret = ak7755_register_read(ak7755, AK7755_REG_C8_DAC_INPUT, &c8);
	if (ret)
		return ret;
	ret = ak7755_register_read(ak7755, AK7755_REG_CE_POWER_MANAGEMENT, &ce);
	if (ret)
		return ret;
	ret = ak7755_register_read(ak7755, AK7755_REG_D4_LINEOUT_VOLUME, &d4);
	if (ret)
		return ret;
	ret = ak7755_register_read(ak7755, AK7755_REG_DA_MUTE_CONTROL, &da);
	if (ret)
		return ret;
	ret = ak7755_register_read(ak7755, AK7755_REG_CF_RESET_POWER, &cf);
	if (ret)
		return ret;

	dev_info(&ak7755->client->dev,
		 "route baseline: C0=%#02x C1=%#02x C2=%#02x C3=%#02x C6=%#02x C7=%#02x C8=%#02x CE=%#02x D4=%#02x DA=%#02x CF=%#02x\n",
		 c0, c1, c2, c3, c6, c7, c8, ce, d4, da, cf);
	return 0;
}

static int ak7755_set_dai_fmt(struct snd_soc_dai *dai, unsigned int fmt)
{
	struct ak7755_priv *ak7755 = snd_soc_component_get_drvdata(dai->component);
	int ret;

	if ((fmt & SND_SOC_DAIFMT_FORMAT_MASK) != SND_SOC_DAIFMT_I2S ||
	    (fmt & SND_SOC_DAIFMT_INV_MASK) != SND_SOC_DAIFMT_NB_NF ||
	    (fmt & SND_SOC_DAIFMT_CLOCK_PROVIDER_MASK) != SND_SOC_DAIFMT_BP_FP)
		return -EINVAL;

	mutex_lock(&ak7755->lock);
	ret = ak7755_log_route_snapshot_locked(ak7755);
	if (ret)
		goto out_unlock;

	/* Factory evidence: 12.288 MHz XTI master, 48 kHz, AINE enabled. */
	ret = ak7755_register_write(ak7755, AK7755_REG_C0_CLOCK_SETTING1, 0x0d);
	if (ret)
		goto out_unlock;

	/* AK7755 drives both BICK and LRCK on the factory board. */
	ret = ak7755_register_write(ak7755, AK7755_REG_CA_CLOCK_OUTPUT, 0x60);
	if (ret)
		goto out_unlock;

	/* Factory route uses 64fs; CKRESETN remains low until PCM prepare. */
	ret = ak7755_register_write(ak7755, AK7755_REG_C1_CLOCK_SETTING2, 0x00);
	if (ret)
		goto out_unlock;

	ret = ak7755_register_write(ak7755, AK7755_REG_C2_SERIAL_FORMAT, 0x10);
	if (ret)
		goto out_unlock;

	/* Recovered Android HAL: DLRAM 2048:6144 and data2 POMODE/DRAM. */
	ret = ak7755_register_write(ak7755, AK7755_REG_C3_DSP_IO, 0x02);
	if (ret)
		goto out_unlock;
	ret = ak7755_register_write(ak7755, AK7755_REG_C4_DATA_RAM, 0x48);
	if (ret)
		goto out_unlock;

	/* data2 DSP DOUT4 -> DAC; no direct SDIN or digital mixer selection. */
	ret = ak7755_register_write(ak7755, AK7755_REG_C6_DAC_FORMAT, 0x00);
	if (ret)
		goto out_unlock;
	ret = ak7755_register_write(ak7755, AK7755_REG_C7_DSP_OUTPUT, 0x00);
	if (ret)
		goto out_unlock;
	ret = ak7755_register_write(ak7755, AK7755_REG_C8_DAC_INPUT, 0x00);
	if (ret)
		goto out_unlock;

	/* OUT3 is the only analog mixer; keep LIN and both DAC switches open. */
	ret = ak7755_register_write(ak7755, AK7755_REG_C9_ANALOG_IO,
				    AK7755_C9_ALL_MIXERS_OFF);
	if (ret)
		goto out_unlock;
	ret = ak7755_register_write(ak7755, AK7755_REG_D3_LINEIN_OUT3_VOLUME,
				    0x0f);
	if (ret)
		goto out_unlock;

	/* HAL explicitly sets both line outputs to 0 dB. */
	ret = ak7755_register_write(ak7755, AK7755_REG_D4_LINEOUT_VOLUME, 0xff);
	if (ret)
		goto out_unlock;

	/* CONT1A.D4 must be one during system reset; start DAC soft-muted. */
	ret = ak7755_register_write(ak7755, AK7755_REG_DA_MUTE_CONTROL,
				    AK7755_DA_DAC_MUTE | AK7755_DA_REQUIRED_ONE);

out_unlock:
	mutex_unlock(&ak7755->lock);
	if (!ret)
		dev_info_once(&ak7755->client->dev,
			      "DAI prepared: I2S 48 kHz stereo S16, codec clock provider, 64fs; factory data2 DSP route staged\n");

	return ret;
}

static int ak7755_set_direct_output_run_locked(struct ak7755_priv *ak7755)
{
	u8 c0, c1, c2, c3, c4, c6, c7, c8, ca, ce, d3, d4, da, cf;
	int ret;

	if (ak7755->direct_output_running)
		return 0;

	/* Match the factory transition: clock first, then analog and DSP cores. */
	ret = ak7755_update_bits(ak7755, AK7755_REG_C1_CLOCK_SETTING2,
				 AK7755_C1_CKRESETN, AK7755_C1_CKRESETN);
	if (ret)
		return ret;

	usleep_range(10000, 11000);
	/* Factory media-speaker route powers DAC L/R plus Lineout1 and Lineout2. */
	ret = ak7755_register_write(ak7755, AK7755_REG_CE_POWER_MANAGEMENT,
				    0x0f);
	if (ret)
		return ret;

	/* Run the CRC-verified data2 DSP; ADC/Linein power remains clear. */
	ret = ak7755_register_write(ak7755, AK7755_REG_CF_RESET_POWER,
				    AK7755_CF_RUN_MASK);
	if (ret)
		goto err_power_down;

	usleep_range(10000, 11000);
	ret = ak7755_register_write(ak7755, AK7755_REG_D4_LINEOUT_VOLUME, 0xff);
	if (ret)
		goto err_power_down;
	msleep(10);

	/* External amplifier remains hardware-muted while DAC soft mute releases. */
	ret = ak7755_update_bits(ak7755, AK7755_REG_DA_MUTE_CONTROL,
				 AK7755_DA_DAC_MUTE | AK7755_DA_REQUIRED_ONE,
				 AK7755_DA_REQUIRED_ONE);
	if (ret)
		goto err_power_down;
	msleep(25);

	ret = ak7755_register_read(ak7755, AK7755_REG_C0_CLOCK_SETTING1, &c0);
	if (ret)
		goto err_power_down;
	ret = ak7755_register_read(ak7755, AK7755_REG_C1_CLOCK_SETTING2, &c1);
	if (ret)
		goto err_power_down;
	ret = ak7755_register_read(ak7755, AK7755_REG_C2_SERIAL_FORMAT, &c2);
	if (ret)
		goto err_power_down;
	ret = ak7755_register_read(ak7755, AK7755_REG_C3_DSP_IO, &c3);
	if (ret)
		goto err_power_down;
	ret = ak7755_register_read(ak7755, AK7755_REG_C4_DATA_RAM, &c4);
	if (ret)
		goto err_power_down;
	ret = ak7755_register_read(ak7755, AK7755_REG_C6_DAC_FORMAT, &c6);
	if (ret)
		goto err_power_down;
	ret = ak7755_register_read(ak7755, AK7755_REG_C7_DSP_OUTPUT, &c7);
	if (ret)
		goto err_power_down;
	ret = ak7755_register_read(ak7755, AK7755_REG_C8_DAC_INPUT, &c8);
	if (ret)
		goto err_power_down;
	ret = ak7755_register_read(ak7755, AK7755_REG_CA_CLOCK_OUTPUT, &ca);
	if (ret)
		goto err_power_down;
	ret = ak7755_register_read(ak7755, AK7755_REG_CE_POWER_MANAGEMENT, &ce);
	if (ret)
		goto err_power_down;
	ret = ak7755_register_read(ak7755, AK7755_REG_D3_LINEIN_OUT3_VOLUME,
				    &d3);
	if (ret)
		goto err_power_down;
	ret = ak7755_register_read(ak7755, AK7755_REG_D4_LINEOUT_VOLUME, &d4);
	if (ret)
		goto err_power_down;
	ret = ak7755_register_read(ak7755, AK7755_REG_DA_MUTE_CONTROL, &da);
	if (ret)
		goto err_power_down;
	ret = ak7755_register_read(ak7755, AK7755_REG_CF_RESET_POWER, &cf);
	if (ret)
		goto err_power_down;
	if (c0 != 0x0d || c1 != 0x01 || c2 != 0x10 || c3 != 0x02 ||
	    c4 != 0x48 || c6 || c7 || c8 || ca != 0x60 || ce != 0x0f ||
	    d3 != 0x0f || d4 != 0xff || da != 0x10 || cf != 0x0c) {
		dev_err(&ak7755->client->dev,
			"FACTORY DSP RUN mismatch: C0=%02x C1=%02x C2=%02x C3=%02x C4=%02x C6=%02x C7=%02x C8=%02x CA=%02x CE=%02x D3=%02x D4=%02x DA=%02x CF=%02x\n",
			c0, c1, c2, c3, c4, c6, c7, c8, ca, ce, d3, d4, da, cf);
		ret = -EIO;
		goto err_power_down;
	}

	ak7755->direct_output_running = true;
	ak7755->analog_boundary = 0;
	ak7755->lineout_volume_stage = 0;
	dev_info(&ak7755->client->dev,
		 "FACTORY DSP RUN verified: C0=%02x C1=%02x C2=%02x C3=%02x C4=%02x C6=%02x C7=%02x C8=%02x CA=%02x CE=%02x D3=%02x D4=%02x DA=%02x CF=%02x\n",
		 c0, c1, c2, c3, c4, c6, c7, c8, ca, ce, d3, d4, da, cf);
	return 0;

err_power_down:
	/* Best-effort fail-safe: mute before reset/power-down on any start error. */
	if (!ak7755_update_bits(ak7755, AK7755_REG_DA_MUTE_CONTROL,
				 AK7755_DA_DAC_MUTE | AK7755_DA_REQUIRED_ONE,
				 AK7755_DA_DAC_MUTE | AK7755_DA_REQUIRED_ONE))
		msleep(25);
	ak7755_update_bits(ak7755, AK7755_REG_CF_RESET_POWER,
			   AK7755_CF_RUN_MASK, 0);
	ak7755_register_write(ak7755, AK7755_REG_CE_POWER_MANAGEMENT, 0);
	return ret;
}

static int ak7755_set_direct_output_standby_locked(struct ak7755_priv *ak7755)
{
	u8 ce, cf;
	int ret;

	if (!ak7755->direct_output_running)
		return 0;

	/* Soft-mute the running DAC before reset and analog power-down. */
	ret = ak7755_update_bits(ak7755, AK7755_REG_DA_MUTE_CONTROL,
				 AK7755_DA_DAC_MUTE | AK7755_DA_REQUIRED_ONE,
				 AK7755_DA_DAC_MUTE | AK7755_DA_REQUIRED_ONE);
	if (ret)
		return ret;
	msleep(25);

	/* Hold codec and DSP cores in reset before powering down the analog path. */
	ret = ak7755_update_bits(ak7755, AK7755_REG_CF_RESET_POWER,
				 AK7755_CF_RUN_MASK, 0);
	if (ret)
		return ret;

	usleep_range(10000, 11000);
	ret = ak7755_register_write(ak7755, AK7755_REG_CE_POWER_MANAGEMENT, 0);
	if (ret)
		return ret;

	usleep_range(1000, 2000);
	ret = ak7755_register_read(ak7755, AK7755_REG_CE_POWER_MANAGEMENT, &ce);
	if (ret)
		return ret;
	ret = ak7755_register_read(ak7755, AK7755_REG_CF_RESET_POWER, &cf);
	if (ret)
		return ret;
	if (ce || (cf & AK7755_CF_RUN_MASK))
		return -EIO;

	ak7755->direct_output_running = false;
	ak7755->analog_boundary = 0;
	ak7755->lineout_volume_stage = 0;
	dev_info(&ak7755->client->dev,
		 "FACTORY DSP STANDBY verified: CE=%#02x CF=%#02x; amplifier controls unchanged\n",
		 ce, cf);
	return 0;
}

int ak7755_component_set_dac_mute(struct snd_soc_component *component,
				   bool mute)
{
	struct ak7755_priv *ak7755 = snd_soc_component_get_drvdata(component);
	u8 da;
	int ret;

	mutex_lock(&ak7755->lock);
	if (!ak7755->direct_output_running || ak7755->analog_boundary) {
		ret = -EPERM;
		goto out_unlock;
	}

	ret = ak7755_update_bits(ak7755, AK7755_REG_DA_MUTE_CONTROL,
				 AK7755_DA_DAC_MUTE | AK7755_DA_REQUIRED_ONE,
				 AK7755_DA_REQUIRED_ONE |
				 (mute ? AK7755_DA_DAC_MUTE : 0));
	if (ret)
		goto out_unlock;
	msleep(25);

	ret = ak7755_register_read(ak7755, AK7755_REG_DA_MUTE_CONTROL, &da);
	if (ret)
		goto out_unlock;
	if ((da & (AK7755_DA_DAC_MUTE | AK7755_DA_REQUIRED_ONE)) !=
	    (AK7755_DA_REQUIRED_ONE | (mute ? AK7755_DA_DAC_MUTE : 0))) {
		ret = -EIO;
		goto out_unlock;
	}

	dev_info(&ak7755->client->dev, "DAC soft mute=%u verified: DA=%#02x\n",
		 mute, da);

out_unlock:
	mutex_unlock(&ak7755->lock);
	return ret;
}
EXPORT_SYMBOL_GPL(ak7755_component_set_dac_mute);

int ak7755_component_set_lineout_volume(struct snd_soc_component *component,
					 unsigned int stage)
{
	struct ak7755_priv *ak7755 = snd_soc_component_get_drvdata(component);
	const char *label;
	u8 c0, c8, c9, ce, cf, d3, d4, da;
	u8 expected_current;
	u8 target;
	int ret;

	if (stage == AK7755_LINEOUT_VOLUME_MINUS14DB) {
		expected_current = AK7755_D4_LINEOUT1_0DB;
		target = AK7755_D4_LINEOUT1_MINUS14DB;
		label = "minus14db";
	} else if (stage == AK7755_LINEOUT_VOLUME_MINUS28DB) {
		expected_current = AK7755_D4_LINEOUT1_MINUS14DB;
		target = AK7755_D4_LINEOUT1_MINUS28DB;
		label = "minus28db";
	} else {
		return -EINVAL;
	}

	mutex_lock(&ak7755->lock);
	if (!ak7755->direct_output_running || ak7755->analog_boundary ||
	    ak7755->lineout_volume_stage != stage - 1) {
		ret = -EPERM;
		goto out_unlock;
	}

	ret = ak7755_register_read(ak7755, AK7755_REG_DA_MUTE_CONTROL, &da);
	if (ret)
		goto out_unlock;
	ret = ak7755_register_read(ak7755, AK7755_REG_D4_LINEOUT_VOLUME, &d4);
	if (ret)
		goto out_unlock;
	if ((da & (AK7755_DA_DAC_MUTE | AK7755_DA_REQUIRED_ONE)) !=
		   (AK7755_DA_DAC_MUTE | AK7755_DA_REQUIRED_ONE) ||
	    (d4 & AK7755_D4_LINEOUT1_VOLUME_MASK) != expected_current) {
		ret = -EPERM;
		goto out_unlock;
	}

	ret = ak7755_update_bits(ak7755, AK7755_REG_D4_LINEOUT_VOLUME,
				 AK7755_D4_LINEOUT1_VOLUME_MASK, target);
	if (ret)
		goto out_unlock;
	msleep(10);

	ret = ak7755_register_read(ak7755, AK7755_REG_C0_CLOCK_SETTING1, &c0);
	if (ret)
		goto out_unlock;
	ret = ak7755_register_read(ak7755, AK7755_REG_C8_DAC_INPUT, &c8);
	if (ret)
		goto out_unlock;
	ret = ak7755_register_read(ak7755, AK7755_REG_C9_ANALOG_IO, &c9);
	if (ret)
		goto out_unlock;
	ret = ak7755_register_read(ak7755, AK7755_REG_CE_POWER_MANAGEMENT, &ce);
	if (ret)
		goto out_unlock;
	ret = ak7755_register_read(ak7755, AK7755_REG_CF_RESET_POWER, &cf);
	if (ret)
		goto out_unlock;
	ret = ak7755_register_read(ak7755, AK7755_REG_D3_LINEIN_OUT3_VOLUME,
				    &d3);
	if (ret)
		goto out_unlock;
	ret = ak7755_register_read(ak7755, AK7755_REG_D4_LINEOUT_VOLUME, &d4);
	if (ret)
		goto out_unlock;
	ret = ak7755_register_read(ak7755, AK7755_REG_DA_MUTE_CONTROL, &da);
	if (ret)
		goto out_unlock;

	if ((c0 & AK7755_C0_ANALOG_INPUT_ENABLE) ||
	    c8 != AK7755_C8_DAC_INPUT_SDIN1 || c9 ||
	    ce != AK7755_CE_DIRECT_OUTPUT_ON ||
	    (cf & (AK7755_CF_LINEIN | AK7755_CF_RUN_MASK |
		   AK7755_CF_ADC2_RIGHT)) != AK7755_CF_CRESETN || d3 ||
	    (d4 & AK7755_D4_LINEOUT1_VOLUME_MASK) != target ||
	    (da & (AK7755_DA_DAC_MUTE | AK7755_DA_REQUIRED_ONE)) !=
		   (AK7755_DA_DAC_MUTE | AK7755_DA_REQUIRED_ONE)) {
		ret = -EIO;
		goto out_unlock;
	}

	ak7755->lineout_volume_stage = stage;
	dev_info(&ak7755->client->dev,
		 "LINEOUT VOLUME stage=%s C0=%#02x C8=%#02x C9=%#02x CE=%#02x CF=%#02x D3=%#02x D4=%#02x DA=%#02x\n",
		 label, c0, c8, c9, ce, cf, d3, d4, da);

out_unlock:
	mutex_unlock(&ak7755->lock);
	return ret;
}
EXPORT_SYMBOL_GPL(ak7755_component_set_lineout_volume);

int ak7755_component_set_analog_boundary(struct snd_soc_component *component,
					  unsigned int boundary)
{
	struct ak7755_priv *ak7755 = snd_soc_component_get_drvdata(component);
	u8 c0, c8, c9, ce, cf, d3, d4, da;
	u8 expected_ce;
	int ret;

	mutex_lock(&ak7755->lock);
	if (!ak7755->direct_output_running || ak7755->lineout_volume_stage) {
		ret = -EPERM;
		goto out_unlock;
	}

	if (boundary == AK7755_ANALOG_BOUNDARY_DAC_OFF) {
		if (ak7755->analog_boundary) {
			ret = -EPERM;
			goto out_unlock;
		}
		ret = ak7755_update_bits(ak7755, AK7755_REG_DA_MUTE_CONTROL,
					 AK7755_DA_DAC_MUTE |
					 AK7755_DA_REQUIRED_ONE,
					 AK7755_DA_DAC_MUTE |
					 AK7755_DA_REQUIRED_ONE);
		if (ret)
			goto out_unlock;
		msleep(25);
		ret = ak7755_update_bits(ak7755,
					 AK7755_REG_CE_POWER_MANAGEMENT,
					 AK7755_CE_DAC_LEFT |
					 AK7755_CE_DAC_RIGHT, 0);
		expected_ce = AK7755_CE_LINEOUT1;
	} else if (boundary == AK7755_ANALOG_BOUNDARY_LINEOUT_HIZ) {
		if (ak7755->analog_boundary != AK7755_ANALOG_BOUNDARY_DAC_OFF) {
			ret = -EPERM;
			goto out_unlock;
		}
		ret = ak7755_update_bits(ak7755,
					 AK7755_REG_CE_POWER_MANAGEMENT,
					 AK7755_CE_LINEOUT1, 0);
		expected_ce = 0;
	} else {
		ret = -EINVAL;
		goto out_unlock;
	}
	if (ret)
		goto out_unlock;
	msleep(10);

	ret = ak7755_register_read(ak7755, AK7755_REG_C0_CLOCK_SETTING1, &c0);
	if (ret)
		goto out_unlock;
	ret = ak7755_register_read(ak7755, AK7755_REG_C8_DAC_INPUT, &c8);
	if (ret)
		goto out_unlock;
	ret = ak7755_register_read(ak7755, AK7755_REG_C9_ANALOG_IO, &c9);
	if (ret)
		goto out_unlock;
	ret = ak7755_register_read(ak7755, AK7755_REG_CE_POWER_MANAGEMENT, &ce);
	if (ret)
		goto out_unlock;
	ret = ak7755_register_read(ak7755, AK7755_REG_CF_RESET_POWER, &cf);
	if (ret)
		goto out_unlock;
	ret = ak7755_register_read(ak7755, AK7755_REG_D3_LINEIN_OUT3_VOLUME,
				    &d3);
	if (ret)
		goto out_unlock;
	ret = ak7755_register_read(ak7755, AK7755_REG_D4_LINEOUT_VOLUME, &d4);
	if (ret)
		goto out_unlock;
	ret = ak7755_register_read(ak7755, AK7755_REG_DA_MUTE_CONTROL, &da);
	if (ret)
		goto out_unlock;

	if ((c0 & AK7755_C0_ANALOG_INPUT_ENABLE) ||
	    c8 != AK7755_C8_DAC_INPUT_SDIN1 || c9 || ce != expected_ce ||
	    (cf & (AK7755_CF_LINEIN | AK7755_CF_RUN_MASK |
		   AK7755_CF_ADC2_RIGHT)) != AK7755_CF_CRESETN || d3 ||
	    (d4 & AK7755_D4_LINEOUT1_VOLUME_MASK) !=
		   AK7755_D4_LINEOUT1_0DB ||
	    (da & (AK7755_DA_DAC_MUTE | AK7755_DA_REQUIRED_ONE)) !=
		   (AK7755_DA_DAC_MUTE | AK7755_DA_REQUIRED_ONE)) {
		ret = -EIO;
		goto out_unlock;
	}

	ak7755->analog_boundary = boundary;
	dev_info(&ak7755->client->dev,
		 "ANALOG BOUNDARY stage=%s C0=%#02x C8=%#02x C9=%#02x CE=%#02x CF=%#02x D3=%#02x D4=%#02x DA=%#02x\n",
		 boundary == AK7755_ANALOG_BOUNDARY_DAC_OFF ?
		 "dac-off-lineout-vmid" : "lineout-hiz",
		 c0, c8, c9, ce, cf, d3, d4, da);

out_unlock:
	mutex_unlock(&ak7755->lock);
	return ret;
}
EXPORT_SYMBOL_GPL(ak7755_component_set_analog_boundary);

static int ak7755_dai_startup(struct snd_pcm_substream *substream,
			      struct snd_soc_dai *dai)
{
	struct ak7755_priv *ak7755 = snd_soc_component_get_drvdata(dai->component);

	mutex_lock(&ak7755->lock);
	ak7755->open_streams++;
	mutex_unlock(&ak7755->lock);

	return 0;
}

static int ak7755_dai_prepare(struct snd_pcm_substream *substream,
			      struct snd_soc_dai *dai)
{
	struct ak7755_priv *ak7755 = snd_soc_component_get_drvdata(dai->component);
	int ret;

	mutex_lock(&ak7755->lock);
	ret = ak7755_set_direct_output_run_locked(ak7755);
	mutex_unlock(&ak7755->lock);
	if (ret)
		dev_err(&ak7755->client->dev,
			"failed to verify factory DSP RUN; codec soft-muted and powered down, retry remains available: %d\n",
			ret);

	return ret;
}

static void ak7755_dai_shutdown(struct snd_pcm_substream *substream,
				struct snd_soc_dai *dai)
{
	struct ak7755_priv *ak7755 = snd_soc_component_get_drvdata(dai->component);
	int ret = 0;

	mutex_lock(&ak7755->lock);
	if (WARN_ON(!ak7755->open_streams))
		goto out_unlock;

	ak7755->open_streams--;
	if (!ak7755->open_streams)
		ret = ak7755_set_direct_output_standby_locked(ak7755);

out_unlock:
	mutex_unlock(&ak7755->lock);
	if (ret) {
		dev_err(&ak7755->client->dev,
			"failed to verify factory DSP STANDBY, asserting reset: %d\n", ret);
		gpiod_set_value_cansleep(ak7755->reset_gpio, 1);
	}
}

static const struct snd_soc_dai_ops ak7755_dai_ops = {
	.startup = ak7755_dai_startup,
	.shutdown = ak7755_dai_shutdown,
	.prepare = ak7755_dai_prepare,
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

	ret = devm_regulator_get_enable_optional(&client->dev, "safe");
	if (ret && ret != -ENODEV)
		return dev_err_probe(&client->dev, ret,
				     "failed to establish amplifier-safe state\n");
	if (ret == -ENODEV)
		dev_info(&client->dev,
			 "amplifier safety is delegated to the machine driver\n");

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

	ret = ak7755_apply_system_reset_contract(ak7755);
	if (ret)
		goto err_assert_reset;

	ret = ak7755_update_bits(ak7755, AK7755_REG_D0_FUNCTION,
				 AK7755_D0_CRCE, AK7755_D0_CRCE);
	if (ret)
		goto err_assert_reset;

	/* Recovered Android HAL contract, required before all data2 downloads. */
	ret = ak7755_register_write(ak7755, AK7755_REG_C3_DSP_IO, 0x02);
	if (ret)
		goto err_assert_reset;
	ret = ak7755_register_write(ak7755, AK7755_REG_C4_DATA_RAM, 0x48);
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
		 "AK7755EN ID 0x55; PRAM/CRAM/OFREG verified, factory DSP route waits for PCM prepare\n");
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
