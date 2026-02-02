// SPDX-License-Identifier: GPL-2.0-only
/*
 * Epticore EM3071x Ambient Light and Proximity Sensor
 *
 * Copyright (c) 2025, FIXME
 *
 * IIO driver for EM3071x. 7-bit I2C address: 0x24.
 *
 * Based on datasheet at https://datasheet4u.com/datasheets/Epticore-Microelectronics/EM30719/1351581
 * and Android source at https://github.com/MediatekAndroidDevelopers/android_kernel_oukitel_k6000_plus/tree/o-8.0.0/drivers/misc/mediatek/alsps/em3071X/
 */

/*
Device register map as follows:

+------+-----------+---------+--------+-------+-----------+----------+--------+-----------+------------+---------+
| ADDR | REG NAME  | BIT 7   | BIT 6  | BIT 5 | BIT 4     | BIT 3    | BIT 2  | BIT 1     | BIT 0      | Default |
+------+-----------+---------+--------+-------+-----------+----------+--------+-----------+------------+---------+
| 0x00 | (n/a)     | PID                                                                               | 0x31    |
+------+-----------+---------+--------+-------+-----------+----------+--------+-----------+------------+---------+
| 0x01 | CONFIG    | PS EN   | PS_SLP | PS_DR[2:0]                   | ALS_EN | ALS_RANGE | ALSIR_MODE | 0xBF    |
+------+-----------+---------+--------+-------+-----------+----------+--------+-----------+------------+---------+
| 0x02 | INTERRUPT | PS FLAG | PS_PRST[1:0]   | (write 0) | ALS_FLAG | ALS_PRST[1:0]      | INT_CTRL   | 0x00    |
+------+-----------+---------+--------+-------+-----------+----------+--------+-----------+------------+---------+
| 0x03 | PS_LT     | PS_LT[7:0]                                                                        | 0x00    |
+------+-----------+---------+--------+-------+-----------+----------+--------+-----------+------------+---------+
| 0x04 | PS_HT     | PS_HT[7:0]                                                                        | 0xFF    |
+------+-----------+---------+--------+-------+-----------+----------+--------+-----------+------------+---------+
| 0x05 | ALSIR_TH1 | ALSIR_LT[7:0]                                                                     | 0x00    |
+------+-----------+---------+--------+-------+-----------+----------+--------+-----------+------------+---------+
| 0x06 | ALSIR_TH2 | ALSIR_HT[3:0]                          ALSIR_LT[11:8]                             | 0xF0    |
+------+-----------+---------+--------+-------+-----------+----------+--------+-----------+------------+---------+
| 0x07 | ALSIR_TH3 | ALSIR_HT[11:4]                                                                    | 0xFF    |
+------+-----------+---------+--------+-------+-----------+----------+--------+-----------+------------+---------+
| 0x08 | PS_DATA   | PS_DATA[7:0]                                                                      | 0x00    |
+------+-----------+---------+--------+-------+-----------+----------+--------+-----------+------------+---------+
| 0x09 | ALSIR_DT1 | ALSIR_DATA[7:0]                                                                   | 0x00    |
+------+-----------+---------+--------+-------+-----------+----------+--------+-----------+------------+---------+
| 0x0A | ALSIR_DT2 | don't use                            | 0x00                                       | 0x00    |
+------+-----------+---------+--------+-------+-----------+----------+--------+-----------+------------+---------+
| 0x0E | TEST1     | (write 0x00)                                                                      | 0x00    |
+------+-----------+---------+--------+-------+-----------+----------+--------+-----------+------------+---------+
| 0x0F | TEST2     | (write 0x00)                                                                      | 0x00    |
+------+-----------+---------+--------+-------+-----------+----------+--------+-----------+------------+---------+
*/
#include <linux/i2c.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/regmap.h>
#include <linux/iio/events.h>
#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>
#include <linux/regulator/consumer.h>

#define EM3071x_DRIVER_NAME			"em3071x"

#define EM3071x_REG_PRODUCTID		0x00
#define EM3071x_REG_CONFIG			0x01
#define EM3071x_REG_INT				0x02
#define EM3071x_REG_PS_LT			0x03
#define EM3071x_REG_PS_HT			0x04
#define EM3071x_REG_ALSIR_TH1		0x05
#define EM3071x_REG_ALSIR_TH2		0x06
#define EM3071x_REG_ALSIR_TH3		0x07
#define EM3071x_REG_PS_DATA			0x08
#define EM3071x_REG_ALSIR_DT1		0x09
#define EM3071x_REG_ALSIR_DT2		0x0A
#define EM3071x_REG_TEST1			0x0E
#define EM3071x_REG_TEST2			0x0F
#define EM3071x_MAX_REG				0x0F

#define EM3071x_STATE_STANDBY		0
#define EM3071x_STATE_EN_ALS		BIT(2)
#define EM3071x_STATE_EN_PS			BIT(7)

#define EM3071x_CHIP_ID_VAL			0x31
static const u8 em3071x_chip_ids[] = {
	EM3071x_CHIP_ID_VAL,
};

// PS initial int threshold - sensor will trigger int when
// value changes from lower-than-lt to higher-than-ht or vice-versa
#define EM3071x_PS_INT_LT_DEFAULT	0x40
#define EM3071x_PS_INT_HT_DEFAULT	0x48

// ALS initial int threshold
// These values should prevent ALS int as suggested by the data sheet,
// but they definitely don't on the actual device
#define EM3071x_ALS_INT_LT_DEFAULT	0x000
#define EM3071x_ALS_INT_HT_DEFAULT	0xFFF

// PS LED current in mA mapped to values of the ps_dr register field
#define EM3071x_PS_LED_CURRENT_DEFAULT 		200
#define EM3071x_PS_LED_CURRENT_AVAILABLE	"15 25 30 50 60 100 120 200"
static const int em3071x_ps_led_current[8] = {
	15,	30,	60, 120, 25, 50, 100, 200
};

// PS sampling frequences in ms (aka sleep mode)
#define EM3071x_PS_SAMP_FREQ_DEFAULT 		100
#define EM3071x_PS_SAMP_FREQ_AVAILABLE		"100 800"
static const int em3071x_ps_samp_freq[2] = {
	100, 800
};

/* Register fields definitions */
#define EM3071x_REGFIELD(name)                                     	\
	   do {                                                        	\
		   data->reg_##name =                                      	\
			   devm_regmap_field_alloc(&client->dev, regmap,       	\
				   em3071x_reg_field_##name);                  		\
		   if (IS_ERR(data->reg_##name)) {                          \
			   dev_err(&client->dev, "reg field alloc failed.\n"); 	\
			   return PTR_ERR(data->reg_##name);                   	\
		   }                                                        \
	   } while (0)

static const struct reg_field em3071x_reg_field_ps_en =
				   REG_FIELD(EM3071x_REG_CONFIG, 7, 7);
static const struct reg_field em3071x_reg_field_ps_slp =
				   REG_FIELD(EM3071x_REG_CONFIG, 6, 6);
static const struct reg_field em3071x_reg_field_ps_dr =
				   REG_FIELD(EM3071x_REG_CONFIG, 3, 5);
static const struct reg_field em3071x_reg_field_als_en =
				   REG_FIELD(EM3071x_REG_CONFIG, 2, 2);

// Data sheet has conflicting info for the following two fields.
// Page 19 says bit 1 is mode, bit 0 is range; page 18 has them
// reversed. The correct one is page 19.
static const struct reg_field em3071x_reg_field_alsir_mode =
				   REG_FIELD(EM3071x_REG_CONFIG, 1, 1);
static const struct reg_field em3071x_reg_field_als_range =
				   REG_FIELD(EM3071x_REG_CONFIG, 0, 0);

static const struct reg_field em3071x_reg_field_flag_psint =
				   REG_FIELD(EM3071x_REG_INT, 7, 7);
static const struct reg_field em3071x_reg_field_flag_alsint =
				   REG_FIELD(EM3071x_REG_INT, 3, 3);

static const struct reg_field em3071x_reg_field_ps_lt =
				   REG_FIELD(EM3071x_REG_PS_LT, 0, 7);

static const struct reg_field em3071x_reg_field_ps_ht =
				   REG_FIELD(EM3071x_REG_PS_HT, 0, 7);

static const struct reg_field em3071x_reg_field_ps_data =
				   REG_FIELD(EM3071x_REG_PS_DATA, 0, 7);

static const struct reg_field em3071x_reg_field_alsir_dt1 =
				   REG_FIELD(EM3071x_REG_ALSIR_DT1, 0, 7);

static const struct reg_field em3071x_reg_field_alsir_dt2 =
				   REG_FIELD(EM3071x_REG_ALSIR_DT2, 0, 3);

static const struct reg_field em3071x_reg_field_ps_offset =
				   REG_FIELD(EM3071x_REG_TEST2, 1, 4);

/* Driver data definition */
struct em3071x_data {
	struct i2c_client *client;
	struct regulator *vcc;
	struct mutex lock;
	bool als_enabled;
	bool ps_enabled;
	bool ps_sleep;
	u8 ps_near_level;
	u64 timestamp;

	struct regmap *regmap;

	struct regmap_field *reg_ps_en;
	struct regmap_field *reg_ps_slp;
	struct regmap_field *reg_ps_dr;
	struct regmap_field *reg_als_en;
	struct regmap_field *reg_als_range;
	struct regmap_field *reg_alsir_mode;

	struct regmap_field *reg_flag_psint;
	struct regmap_field *reg_flag_alsint;

	struct regmap_field *reg_ps_lt;
	struct regmap_field *reg_ps_ht;

	struct regmap_field *reg_ps_data;
	struct regmap_field *reg_ps_offset;

	struct regmap_field *reg_alsir_dt1;
	struct regmap_field *reg_alsir_dt2;
};

static int em3071x_get_index(const int array[], int array_len, int val)
{
	int i;

	for (i = 0; i < array_len; i++) {
		if (val == array[i]) return i;
	}

	return -EINVAL;
}

static ssize_t em3071x_read_near_level(struct iio_dev *indio_dev,
					  uintptr_t priv,
					  const struct iio_chan_spec *chan,
					  char *buf)
{
	struct em3071x_data *data = iio_priv(indio_dev);

	return sprintf(buf, "%u\n", data->ps_near_level);
}

static ssize_t em3071x_read_ps_led_current(struct iio_dev *indio_dev,
					  uintptr_t priv,
					  const struct iio_chan_spec *chan,
					  char *buf)
{
	int ret;
	struct em3071x_data *data = iio_priv(indio_dev);
	struct i2c_client *client = data->client;

	if (chan->type != IIO_PROXIMITY)
		return -EINVAL;

	mutex_lock(&data->lock);

	unsigned int val;
	ret = regmap_field_read(data->reg_ps_dr, &val);
	if (ret < 0) {
		dev_err(&client->dev, "PS led drive register read failed\n");
		mutex_unlock(&data->lock);
		return ret;
	}

	mutex_unlock(&data->lock);

	if (val > ARRAY_SIZE(em3071x_ps_led_current))
		return -EINVAL;

	return sprintf(buf, "%d\n", em3071x_ps_led_current[val]);
}

static ssize_t em3071x_write_ps_led_current(struct iio_dev *indio_dev,
					  uintptr_t priv,
					  const struct iio_chan_spec *chan,
					  const char *buf, size_t len)
{
	int ret;
	struct em3071x_data *data = iio_priv(indio_dev);
	struct i2c_client *client = data->client;

	if (chan->type != IIO_PROXIMITY)
		return -EINVAL;

	int val;
	ret = kstrtouint(buf, 10, &val);
	if (ret < 0)
		return ret;

	unsigned int led_current_bits;
	led_current_bits = em3071x_get_index(
		em3071x_ps_led_current, ARRAY_SIZE(em3071x_ps_led_current), val);
	if (ret < 0)
		return ret;
	
	mutex_lock(&data->lock);

	ret = regmap_field_write(data->reg_ps_dr, led_current_bits);
	if (ret < 0) {
		dev_err(&client->dev, "PS led drive register read failed\n");
		mutex_unlock(&data->lock);
		return ret;
	}

	mutex_unlock(&data->lock);

	return ret ? ret : len;
}

static const struct iio_event_spec em3071x_events[] = {
	// Proximity event
	{
		.type = IIO_EV_TYPE_THRESH,
		.dir = IIO_EV_DIR_RISING,
		.mask_separate = BIT(IIO_EV_INFO_VALUE),
	},
	// Out-of-proximity event
	{
		.type = IIO_EV_TYPE_THRESH,
		.dir = IIO_EV_DIR_FALLING,
		.mask_separate = BIT(IIO_EV_INFO_VALUE),
	},
};

static const struct iio_chan_spec_ext_info em3071x_ext_ps_info[] = {
	{
		.name = "nearlevel",
		.shared = IIO_SEPARATE,
		.read = em3071x_read_near_level,
	},
	{
		.name = "led_current",
		.shared = IIO_SEPARATE,
		.read  = em3071x_read_ps_led_current,
		.write = em3071x_write_ps_led_current,
	},
	{ }
};

static const struct iio_chan_spec em3071x_channels[] = {
	{
		.type = IIO_LIGHT,
		.info_mask_separate =
			BIT(IIO_CHAN_INFO_ENABLE) |
			BIT(IIO_CHAN_INFO_RAW) |
			BIT(IIO_CHAN_INFO_SCALE),
	},
	{
		.type = IIO_PROXIMITY,
		.info_mask_separate =
			BIT(IIO_CHAN_INFO_ENABLE) |
			BIT(IIO_CHAN_INFO_RAW) |
			BIT(IIO_CHAN_INFO_OFFSET) |
			BIT(IIO_CHAN_INFO_SAMP_FREQ),
		.event_spec = em3071x_events,
		.num_event_specs = ARRAY_SIZE(em3071x_events),
		.ext_info = em3071x_ext_ps_info,
	}
};

static IIO_CONST_ATTR(in_proximity_led_current_available_ma, 
	EM3071x_PS_LED_CURRENT_AVAILABLE);

static IIO_CONST_ATTR(in_proximity_sampling_frequency_available_ms, 
	EM3071x_PS_SAMP_FREQ_AVAILABLE);

static struct attribute *em3071x_attributes[] = {
	&iio_const_attr_in_proximity_led_current_available_ma.dev_attr.attr,
	&iio_const_attr_in_proximity_sampling_frequency_available_ms.dev_attr.attr,
	NULL,
};

static const struct attribute_group em3071x_attribute_group = {
	   .attrs = em3071x_attributes
};

static int em3071x_check_chip_id(const u8 chip_id)
{
	for (int i = 0; i < ARRAY_SIZE(em3071x_chip_ids); i++) {
		if (chip_id == em3071x_chip_ids[i])
			return 0;
	}

	return -ENODEV;
}

static int em3071x_set_state(struct em3071x_data *data, u8 state)
{
	int ret;
	struct i2c_client *client = data->client;

	// 2-bit state; only bit 2 and 7 are supported.
	if (state & ~(EM3071x_STATE_EN_PS | EM3071x_STATE_EN_ALS))
		return -EINVAL;

	mutex_lock(&data->lock);

	// Toggle ALS sensor
	ret = regmap_field_write(data->reg_als_en, !!(state & EM3071x_STATE_EN_ALS));
	if (ret < 0) {
		dev_err(&client->dev, "failed to change als sensor state\n");
	} else {
		data->als_enabled  = !!(state & EM3071x_STATE_EN_ALS);
	}

	// Toggle PS sensor
	ret = regmap_field_write(data->reg_ps_en, !!(state & EM3071x_STATE_EN_PS));
	if (ret < 0) {
		dev_err(&client->dev, "failed to change ps sensor state\n");
	} else {
		data->ps_enabled = !!(state & EM3071x_STATE_EN_PS);
	}

	mutex_unlock(&data->lock);

	return ret;
}

static int em3071x_read_event(struct iio_dev *indio_dev,
				 const struct iio_chan_spec *chan,
				 enum iio_event_type type,
				 enum iio_event_direction dir,
				 enum iio_event_info info,
				 int *val, int *val2)
{
	struct regmap_field *field;
	unsigned int th_data;
	int ret;
	struct em3071x_data *data = iio_priv(indio_dev);

	if (info != IIO_EV_INFO_VALUE)
		return -EINVAL;

	// Only proximity interrupts are implemented at the moment.
	if (dir == IIO_EV_DIR_RISING)
		field = data->reg_ps_ht;
	else if (dir == IIO_EV_DIR_FALLING)
		field = data->reg_ps_lt;
	else
		return -EINVAL;

	mutex_lock(&data->lock);
	ret = regmap_field_read(field, &th_data);
	mutex_unlock(&data->lock);
	if (ret < 0) {
		dev_err(&data->client->dev, "register read failed\n");
		return ret;
	}
	*val = th_data;

	return IIO_VAL_INT;
}

static int em3071x_write_event(struct iio_dev *indio_dev,
				  const struct iio_chan_spec *chan,
				  enum iio_event_type type,
				  enum iio_event_direction dir,
				  enum iio_event_info info,
				  int val, int val2)
{
	struct regmap_field *field;
	int ret;
	struct em3071x_data *data = iio_priv(indio_dev);
	struct i2c_client *client = data->client;

	if (val < 0 || val > 0xFF)
	 	return -EINVAL;

	if (dir == IIO_EV_DIR_RISING)
		field = data->reg_ps_ht;
	else if (dir == IIO_EV_DIR_FALLING)
		field = data->reg_ps_lt;
	else
		return -EINVAL;

	mutex_lock(&data->lock);
	ret = regmap_field_write(field, val);
	mutex_unlock(&data->lock);
	if (ret < 0)
		dev_err(&client->dev, "failed to set PS threshold!\n");

	return ret;
}

static int em3071x_read_raw(struct iio_dev *indio_dev,
			   struct iio_chan_spec const *chan,
			   int *val, int *val2, long mask)
{
	int ret;
	struct em3071x_data *data = iio_priv(indio_dev);
	struct i2c_client *client = data->client;

	if (chan->type != IIO_LIGHT && chan->type != IIO_PROXIMITY)
		return -EINVAL;

	switch (mask) {
	case IIO_CHAN_INFO_ENABLE:
		if (chan->type == IIO_LIGHT)
			*val = data->als_enabled;
		else
			*val = data->ps_enabled;

		return IIO_VAL_INT;

	case IIO_CHAN_INFO_RAW:
		if (chan->type == IIO_LIGHT && data->als_enabled)
		{
			mutex_lock(&data->lock);

			unsigned int dt1, dt2;
			ret = regmap_field_read(data->reg_alsir_dt1, &dt1);
			if (ret < 0) {
				dev_err(&client->dev, "alsir_dt1 register read failed\n");
				mutex_unlock(&data->lock);
				return ret;
			}

			ret = regmap_field_read(data->reg_alsir_dt2, &dt2);
			if (ret < 0) {
				dev_err(&client->dev, "alsir_dt2 register read failed\n");
				mutex_unlock(&data->lock);
				return ret;
			}

			mutex_unlock(&data->lock);

			*val = (dt2 << 8) | dt1;
		}
		else if (chan->type == IIO_PROXIMITY && data->ps_enabled)
		{
			mutex_lock(&data->lock);

			ret = regmap_field_read(data->reg_ps_data, val);
			if (ret < 0) {
				dev_err(&client->dev, "PS register read failed\n");
				mutex_unlock(&data->lock);
				return ret;
			}

			mutex_unlock(&data->lock);
		}

		return IIO_VAL_INT;
	
	case IIO_CHAN_INFO_SCALE:
		if (chan->type == IIO_LIGHT)
		{
			// According to data sheet, the ALS/lux curve
			// is well approximated by lux = ALS / 2.47
			*val = 100;
			*val2 = 247;

			return IIO_VAL_FRACTIONAL;
		}

		return -EINVAL;

	case IIO_CHAN_INFO_OFFSET:
		if (chan->type == IIO_PROXIMITY)
		{
			mutex_lock(&data->lock);

			unsigned int offset_bits;
			ret = regmap_field_read(data->reg_ps_offset, &offset_bits);
			if (ret < 0) {
				dev_err(&client->dev, "ps offset register read failed\n");
				mutex_unlock(&data->lock);
				return ret;
			}

			mutex_unlock(&data->lock);

			// Actual offset according to datasheet as decimal counts
			*val = -(offset_bits << 5);
			return IIO_VAL_INT;
		}

		return -EINVAL;

	case IIO_CHAN_INFO_SAMP_FREQ:
		if (chan->type == IIO_PROXIMITY)
		{
			mutex_lock(&data->lock);

			unsigned int sleep;
			ret = regmap_field_read(data->reg_ps_slp, &sleep);
			if (ret < 0) {
				dev_err(&client->dev, "ps sleep register read failed\n");
				mutex_unlock(&data->lock);
				return ret;
			}

			mutex_unlock(&data->lock);

			// Sampling every 100ms when active, 800ms when sleeping
			*val = sleep ? 800: 100;
			return IIO_VAL_INT;
		}

		return -EINVAL;

	default:
		return -EINVAL;
	}
}

static int em3071x_write_raw(struct iio_dev *indio_dev,
				struct iio_chan_spec const *chan,
				int val, int val2, long mask)
{
	int ret;
	struct em3071x_data *data = iio_priv(indio_dev);
	struct i2c_client *client = data->client;

	if (chan->type != IIO_LIGHT && chan->type != IIO_PROXIMITY)
		return -EINVAL;

	switch (mask) {
	case IIO_CHAN_INFO_ENABLE:
		if (val < 0 || val > 1)
			return -EINVAL;

		u8 state;
		if (chan->type == IIO_LIGHT)
		{
			// Already enabled, nothing to do
			if ((data->als_enabled && val) || (!data->als_enabled && !val))
				return 0;

			state = (data->ps_enabled ? EM3071x_STATE_EN_PS: 0) |
				(val ? EM3071x_STATE_EN_ALS: 0);
		}
		else
		{
			// Already disabled, nothing to do
			if ((data->ps_enabled && val) || (!data->ps_enabled && !val))
				return 0;

			state = (val ? EM3071x_STATE_EN_PS: 0) |
				(data->als_enabled ? EM3071x_STATE_EN_ALS: 0);
		}

		ret = em3071x_set_state(data, state);
		return ret;

	case IIO_CHAN_INFO_OFFSET:
		if (chan->type == IIO_PROXIMITY)
		{
			// Only negative offsets are supported
			if (val > 0)
				return -EINVAL;

			mutex_lock(&data->lock);

			unsigned int offset_bits = ((-val) >> 5) & 0b1111;
			ret = regmap_field_write(data->reg_ps_offset, offset_bits);
			if (ret < 0)
				dev_err(&client->dev, "failed to write ps offset register\n");

			mutex_unlock(&data->lock);
		
			return ret;
		}

		return -EINVAL;

	case IIO_CHAN_INFO_SAMP_FREQ:
		if (chan->type == IIO_PROXIMITY)
		{
			// Sample frequency must be positive
			if (val <= 0)
				return -EINVAL;

			bool enable_sleep = val >= 800 ? 1: 0;
			if (enable_sleep == data->ps_sleep)
				return 0;

			mutex_lock(&data->lock);

			ret = regmap_field_write(data->reg_ps_slp, enable_sleep);
			if (ret < 0) {
				dev_err(&client->dev, "ps sleep register write failed\n");
				mutex_unlock(&data->lock);
				return ret;
			}

			data->ps_sleep = enable_sleep;

			mutex_unlock(&data->lock);

			return ret;
		}

		return -EINVAL;

	default:
		return -EINVAL;
	}
}

static const struct iio_info em3071x_info = {
	.read_raw               = em3071x_read_raw,
	.write_raw              = em3071x_write_raw,
    .attrs                  = &em3071x_attribute_group,
	.read_event_value       = em3071x_read_event,
	.write_event_value      = em3071x_write_event,
};

static int em3071x_init(struct iio_dev *indio_dev)
{
	int ret;
	int chipid;
	u8 state;
	struct em3071x_data *data = iio_priv(indio_dev);
	struct i2c_client *client = data->client;

	mutex_lock(&data->lock);

	// Retrieve the chip ID
	ret = regmap_read(data->regmap, EM3071x_REG_PRODUCTID, &chipid);
	if (ret < 0) {
		dev_err(&client->dev, "product id read failed\n");
		goto return_mutex_unlock;
	}

	// Print an info message if the chip is unknown
	ret = em3071x_check_chip_id(chipid);
	if (ret < 0)
		dev_info(&client->dev, "new unknown chip id: 0x%x\n", chipid);

	// Init sequence
	// 1. Disable and power down
	ret = regmap_write(data->regmap, EM3071x_REG_CONFIG, 0x00);
	if (ret < 0) {
		dev_err(&client->dev, "failed to disable and power down sensor\n");
		goto return_mutex_unlock;
	}

	// 2. Clear all interrupt flag
	ret = regmap_write(data->regmap, EM3071x_REG_INT, 0x00);
	if (ret < 0) {
		dev_err(&client->dev, "failed to clear interrupt flag\n");
		goto return_mutex_unlock;
	}

	// 3. Initialize reset register (?)
	ret = regmap_write(data->regmap, EM3071x_REG_TEST1, 0x00);
	if (ret < 0) {
		dev_err(&client->dev, "failed to initialize reset register\n");
		goto return_mutex_unlock;
	}

	// 4. Write initial PS Low threshold value
	ret = regmap_field_write(data->reg_ps_lt, EM3071x_PS_INT_LT_DEFAULT);
	if (ret < 0) {
		dev_err(&client->dev, "failed to initialize ps low threshold\n");
		goto return_mutex_unlock;
	}

	// 5. Write initial PS High threshold value
	ret = regmap_field_write(data->reg_ps_ht, EM3071x_PS_INT_HT_DEFAULT);
	if (ret < 0) {
		dev_err(&client->dev, "failed to initialize ps high threshold\n");
		goto return_mutex_unlock;
	}
	
	// 6. Write initial PS offset value
	ret = regmap_write(data->regmap, EM3071x_REG_TEST2, 0x00);
	if (ret < 0) {
		dev_err(&client->dev, "failed to initialize ps offset\n");
		goto return_mutex_unlock;
	}
	
	// 7. Write initial PS sampling frequency
	unsigned int ps_samp_freq_bits = em3071x_get_index(
		em3071x_ps_samp_freq, ARRAY_SIZE(em3071x_ps_samp_freq), 
		EM3071x_PS_SAMP_FREQ_DEFAULT);
	ret = regmap_field_write(data->reg_ps_dr, ps_samp_freq_bits);
	if (ret < 0) {
		dev_err(&client->dev, "failed to initialize ps sampling frequency\n");
		goto return_mutex_unlock;
	}
	
	// 8. Write initial ALS Initial threshold values
	// Lower two bytes of LT
	ret = regmap_write(data->regmap, EM3071x_REG_ALSIR_TH1, 
		EM3071x_ALS_INT_LT_DEFAULT & 0x0FF);
	if (ret < 0) {
		dev_err(&client->dev, "failed to initialize als threshold 1\n");
		goto return_mutex_unlock;
	}

	// Lower byte of HT + Upper byte of LT
	ret = regmap_write(data->regmap, EM3071x_REG_ALSIR_TH2, 
		((EM3071x_ALS_INT_HT_DEFAULT & 0x00F) << 4) | 
		((EM3071x_ALS_INT_LT_DEFAULT & 0xF00) >> 8));
	if (ret < 0) {
		dev_err(&client->dev, "failed to initialize als threshold 2\n");
		goto return_mutex_unlock;
	}

	// Upper two bytes of HT
	ret = regmap_write(data->regmap, EM3071x_REG_ALSIR_TH3, 
		(EM3071x_ALS_INT_HT_DEFAULT & 0xFF0) >> 4);
	if (ret < 0) {
		dev_err(&client->dev, "failed to initialize als threshold 3\n");
		goto return_mutex_unlock;
	}

	// 9. Set the initial PS LED current
	unsigned int ps_led_current_bits = em3071x_get_index(
		em3071x_ps_led_current, ARRAY_SIZE(em3071x_ps_led_current), 
		EM3071x_PS_LED_CURRENT_DEFAULT);
	ret = regmap_field_write(data->reg_ps_dr, ps_led_current_bits);
	if (ret < 0) {
		dev_err(&client->dev, "failed to set ps led current\n");
		goto return_mutex_unlock;
	}

	// 10. Set ALS sensing mode to store ambient light detection value
	ret = regmap_field_write(data->reg_alsir_mode, 1);
	if (ret < 0) {
		dev_err(&client->dev, "failed to set als sensing mode\n");
		goto return_mutex_unlock;
	}

	mutex_unlock(&data->lock);

	// Turn on both sensors
	state = EM3071x_STATE_EN_ALS | EM3071x_STATE_EN_PS;
	ret = em3071x_set_state(data, state);
	if (ret < 0) {
		dev_err(&client->dev, "failed to enable sensor\n");
		return ret;
	}

	return 0;

return_mutex_unlock:
	mutex_unlock(&data->lock);

	return ret;
}

static bool em3071x_is_volatile_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case EM3071x_REG_ALSIR_DT1:
	case EM3071x_REG_ALSIR_DT2:
	case EM3071x_REG_PS_DATA:
	case EM3071x_REG_INT:
		return true;
	default:
		return false;
	}
}

static const struct regmap_config em3071x_regmap_config = {
	.name = "em3071x_regmap",
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = EM3071x_MAX_REG,
	.cache_type = REGCACHE_RBTREE,
	.volatile_reg = em3071x_is_volatile_reg,
};

static int em3071x_regmap_init(struct em3071x_data *data)
{
	struct regmap *regmap;
	struct i2c_client *client = data->client;

	regmap = devm_regmap_init_i2c(client, &em3071x_regmap_config);
	if (IS_ERR(regmap)) {
		dev_err(&client->dev, "regmap initialization failed.\n");
		return PTR_ERR(regmap);
	}
	data->regmap = regmap;

	EM3071x_REGFIELD(alsir_mode);
	EM3071x_REGFIELD(als_range);
	EM3071x_REGFIELD(als_en);
	EM3071x_REGFIELD(ps_dr);
	EM3071x_REGFIELD(ps_slp);
	EM3071x_REGFIELD(ps_en);
	EM3071x_REGFIELD(flag_psint);
	EM3071x_REGFIELD(flag_alsint);
	EM3071x_REGFIELD(ps_ht);
	EM3071x_REGFIELD(ps_lt);
	EM3071x_REGFIELD(ps_data);
	EM3071x_REGFIELD(alsir_dt1);
	EM3071x_REGFIELD(alsir_dt2);
	EM3071x_REGFIELD(ps_offset);

	return 0;
}

static irqreturn_t em3071x_irq_handler(int irq, void *private)
{
	struct iio_dev *indio_dev = private;
	struct em3071x_data *data = iio_priv(indio_dev);

	data->timestamp = iio_get_time_ns(indio_dev);

	return IRQ_WAKE_THREAD;
}

static irqreturn_t em3071x_irq_event_handler(int irq, void *private)
{
	int ret;
	u64 event1, event2;

	struct iio_dev *indio_dev = private;
	struct em3071x_data *data = iio_priv(indio_dev);
	struct i2c_client *client = data->client;

	mutex_lock(&data->lock);

	// 1. Check whether it's a PS int or an ALS int
	unsigned int flag_psint;
	ret = regmap_field_read(data->reg_flag_psint, &flag_psint);
	if (ret < 0) {
		dev_err(&client->dev, "register flag_psint read failed: %d\n", ret);
		goto out;
	}

	// We're not interested in ALS int, quit early if it is one
	if (!flag_psint)
		goto out;

	// 2. Clear the entire int register (PS int seems to set ALS int flag too)
	ret = regmap_write(data->regmap, EM3071x_REG_INT, 0);
	if (ret < 0)
		dev_err(&client->dev, "failed to reset interrupts\n");

	// 3. Read the PS register value
	unsigned int ps_data;
	ret = regmap_field_read(data->reg_ps_data, &ps_data);
	if (ret < 0) {
		dev_err(&client->dev, "failed to read ps data in int handler\n");
		goto out;
	}

	// 3. Read the PS LT/HT registers value
	unsigned int ps_ht_data, ps_lt_data;
	ret = regmap_field_read(data->reg_ps_ht, &ps_ht_data);
	if (ret < 0) {
		dev_err(&client->dev, "failed to read ps ht data\n");
		goto out;
	}

	ret = regmap_field_read(data->reg_ps_lt, &ps_lt_data);
	if (ret < 0) {
		dev_err(&client->dev, "failed to read ps ht data\n");
		goto out;
	}

	// 4. Report the IIO event
	bool in_proximity = ps_data >= ps_ht_data;
	bool out_of_proximity = ps_data <= ps_lt_data;
	if (in_proximity || out_of_proximity) {
		event1 = IIO_UNMOD_EVENT_CODE(IIO_PROXIMITY, 1,
				IIO_EV_TYPE_THRESH,
				(out_of_proximity ? IIO_EV_DIR_FALLING : IIO_EV_DIR_RISING));
		iio_push_event(indio_dev, event1, data->timestamp);
	}

	// 5. Clear the PS int again
	ret = regmap_write(data->regmap, EM3071x_REG_INT, 0);
	if (ret < 0)
		dev_err(&client->dev, "failed to reset interrupts\n");

	// 6. Read the PS register value again
	ret = regmap_field_read(data->reg_ps_data, &ps_data);
	if (ret < 0) {
		dev_err(&client->dev, "failed to read ps data in int handler\n");
		goto out;
	}

	// 7. Report the IIO event again if different from before
	in_proximity = ps_data >= ps_ht_data;
	out_of_proximity = ps_data <= ps_lt_data;
	if (in_proximity || out_of_proximity) {
		event2 = IIO_UNMOD_EVENT_CODE(IIO_PROXIMITY, 1,
				IIO_EV_TYPE_THRESH,
				(out_of_proximity ? IIO_EV_DIR_FALLING : IIO_EV_DIR_RISING));

		if (event2 != event1)
			iio_push_event(indio_dev, event2, data->timestamp);
	}

out:
	mutex_unlock(&data->lock);

	return IRQ_HANDLED;
}

static int em3071x_probe(struct i2c_client *client)
{
	dev_info(&client->dev, "Probing em3071x ALS/PS sensor at 0x%02x\n", 
		client->addr);

	// Allocate driver state structure
	int ret;
	struct iio_dev *indio_dev;
	struct em3071x_data *data;

	indio_dev = devm_iio_device_alloc(&client->dev, sizeof(*data));
	if (!indio_dev)
		return -ENOMEM;

	data = iio_priv(indio_dev);
	i2c_set_clientdata(client, indio_dev);
	data->client = client;

	// Turn on the regulator
	data->vcc = devm_regulator_get(&client->dev, "vcc");
	if (IS_ERR(data->vcc))
		return dev_err_probe(&client->dev, PTR_ERR(data->vcc),
			 "failed to get vcc regulator\n");

	ret = regulator_enable(data->vcc);
	if (ret) {
		dev_err(&client->dev, "failed to enable vcc regulator\n");
		return ret;
	}

	// Retrieve from DT the level for 'near' detection.
	// If not set use 230, which appears to be reasonable according to 
	// datasheet and actual device experience
	data->ps_near_level = 230;
	device_property_read_u8(&client->dev, "proximity-near-level",
    	&data->ps_near_level);

	mutex_init(&data->lock);

	// Prepare the regmap
	ret = em3071x_regmap_init(data);
	if (ret < 0) {
		dev_err(&client->dev, "em3071x_regmap_init failed\n");
		goto err_disable_vcc;
	}

	indio_dev->info = &em3071x_info;
	indio_dev->name = EM3071x_DRIVER_NAME;
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->channels = em3071x_channels;
	indio_dev->num_channels = ARRAY_SIZE(em3071x_channels);

	ret = em3071x_init(indio_dev);
	if (ret < 0) {
		dev_err(&client->dev, "em3071x_init failed\n");
		goto err_disable_vcc;
	}

	// Prepare the PS IRQ
	if (client->irq > 0) {
		ret = devm_request_threaded_irq(&client->dev, client->irq,
			em3071x_irq_handler,
			em3071x_irq_event_handler,
			IRQF_TRIGGER_LOW | IRQF_ONESHOT | IRQF_SHARED,
			"em3071x_event", indio_dev);
		if (ret < 0) {
			dev_err(&client->dev, "request irq %d failed\n",
				client->irq);
			goto err_standby;
		}
	}

	// Register the driver with the iio subsystem
	ret = devm_iio_device_register(&client->dev, indio_dev);
	if (ret < 0) {
		dev_err(&client->dev, "iio_device_register failed\n");
		goto err_standby;
	}

	return 0;

err_standby:
	em3071x_set_state(data, EM3071x_STATE_STANDBY);

err_disable_vcc:
	regulator_disable(data->vcc);

	return ret;
}

static void em3071x_remove(struct i2c_client *client)
{
	dev_info(&client->dev, "Removing em3071x ALS/PS sensor driver\n");

	struct iio_dev *indio_dev = i2c_get_clientdata(client);
	struct em3071x_data *data = iio_priv(indio_dev);

	// No need to call iio_device_unregister here
	// as we're using devm_iio_device_register to register

	// Put the device into standby
	int ret = em3071x_set_state(data, EM3071x_STATE_STANDBY);
	if (ret < 0)
		dev_err(&client->dev, "failed to set standby state during remove\n");

	// Turn off power
	ret = regulator_disable(data->vcc);
	if (ret < 0)
		dev_err(&client->dev, "failed to disable vcc regulator during remove\n");
}

static int em3071x_suspend(struct device *dev)
{
	struct iio_dev *indio_dev = i2c_get_clientdata(to_i2c_client(dev));
	struct em3071x_data *data = iio_priv(indio_dev);

	// Put the device into standby
	int ret = em3071x_set_state(data, EM3071x_STATE_STANDBY);
	if (ret < 0)
		dev_err(dev, "failed to set standby state during suspend\n");

	// Turn off power
	ret = regulator_disable(data->vcc);
	if (ret < 0)
		dev_err(dev, "failed to disable vcc regulator during suspend\n");

	return ret;
}

static int em3071x_resume(struct device *dev)
{
	struct iio_dev *indio_dev = i2c_get_clientdata(to_i2c_client(dev));
	struct em3071x_data *data = iio_priv(indio_dev);

	// Turn on power
	int ret = regulator_enable(data->vcc);
	if (ret) {
		dev_err(dev, "failed to enable vcc regulator in resume path\n");
		return ret;
	}

	// Restore the device operational mode as it was before suspend
	u8 state = 0;

	if (data->ps_enabled)
		state |= EM3071x_STATE_EN_PS;
	if (data->als_enabled)
		state |= EM3071x_STATE_EN_ALS;

	ret = em3071x_set_state(data, state);
	if (ret < 0)
		dev_err(dev, "failed to set initial state in resume path\n");

	return ret;
}

// I2C Device ID
static DEFINE_SIMPLE_DEV_PM_OPS(em3071x_pm_ops, em3071x_suspend,
				   em3071x_resume);

static const struct i2c_device_id em3071x_i2c_id[] = {
	{ "EM3071x" },
	{ }
};
MODULE_DEVICE_TABLE(i2c, em3071x_i2c_id);

// Device tree matching rules
static const struct of_device_id em3071x_of_match[] = {
	{ .compatible = "epticore,em3071x", },
	{ }
};
MODULE_DEVICE_TABLE(of, em3071x_of_match);

// I2C device driver declaration
static struct i2c_driver em3071x_driver = {
	.driver = {
		.name = EM3071x_DRIVER_NAME,
		.of_match_table = em3071x_of_match,
		.pm = pm_sleep_ptr(&em3071x_pm_ops),
	},
	.probe =	em3071x_probe,
	.remove =	em3071x_remove,
	.id_table =	em3071x_i2c_id,
};

module_i2c_driver(em3071x_driver);

MODULE_AUTHOR("FIXME");
MODULE_DESCRIPTION("EM3071x Ambient Light and Proximity Sensor driver");
MODULE_LICENSE("GPL v2");