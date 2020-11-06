/*
 * Copyright (c) 2020 PHYTEC Messtechnik GmbH, Zhidong Huang
 *	
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT gooddisplay_il0323

#include <string.h>
#include <device.h>
#include <init.h>
#include <drivers/display.h>
#include <drivers/gpio.h>
#include <drivers/spi.h>
#include <sys/byteorder.h>

#include "il0323_regs.h"

#include <logging/log.h>
LOG_MODULE_REGISTER(il0323, CONFIG_ZMK_LOG_LEVEL);
#include <stdio.h>

/**
 * IL0323(UC8175) compatible EPD controller driver.
 * GoodDisplay GDEW0102T4
 * Currently only the black/white pannels are supported (KW mode),
 * also first gate/source should be 0.
 */

#define IL0323_SPI_FREQ DT_INST_PROP(0, spi_max_frequency)
#define IL0323_BUS_NAME DT_INST_BUS_LABEL(0)
#define IL0323_DC_PIN DT_INST_GPIO_PIN(0, dc_gpios)
#define IL0323_DC_FLAGS DT_INST_GPIO_FLAGS(0, dc_gpios)
#define IL0323_DC_CNTRL DT_INST_GPIO_LABEL(0, dc_gpios)
#define IL0323_CS_PIN DT_INST_SPI_DEV_CS_GPIOS_PIN(0)
#if DT_INST_SPI_DEV_HAS_CS_GPIOS(0)
#define IL0323_CS_CNTRL DT_INST_SPI_DEV_CS_GPIOS_LABEL(0)
#endif

#define IL0323_BUSY_PIN DT_INST_GPIO_PIN(0, busy_gpios)
#define IL0323_BUSY_CNTRL DT_INST_GPIO_LABEL(0, busy_gpios)
#define IL0323_BUSY_FLAGS DT_INST_GPIO_FLAGS(0, busy_gpios)
#define IL0323_RESET_PIN DT_INST_GPIO_PIN(0, reset_gpios)
#define IL0323_RESET_CNTRL DT_INST_GPIO_LABEL(0, reset_gpios)
#define IL0323_RESET_FLAGS DT_INST_GPIO_FLAGS(0, reset_gpios)

#define EPD_PANEL_WIDTH			DT_INST_PROP(0, width)
#define EPD_PANEL_HEIGHT		DT_INST_PROP(0, height)
#define IL0323_PIXELS_PER_BYTE		8U

/* Horizontally aligned page! */
#define IL0323_NUMOF_PAGES		(EPD_PANEL_WIDTH / IL0323_PIXELS_PER_BYTE)
#define IL0323_PANEL_FIRST_GATE		0U
#define IL0323_PANEL_LAST_GATE		(EPD_PANEL_HEIGHT - 1)
#define IL0323_PANEL_FIRST_PAGE		0U
#define IL0323_PANEL_LAST_PAGE		(IL0323_NUMOF_PAGES - 1)

/* Select refresh mode */
#define IL0323_PARTIAL_REFRESH	1U
// #ifdef CONFIG_LVGL_IL0323_PARTIAL_REFRESH
// #define IL0323_PARTIAL_REFRESH		1U
// #else 
// #define IL0323_PARTIAL_REFRESH		1U
// #endif

struct il0323_data {
	struct device *reset;
	struct device *dc;
	struct device *busy;
	struct device *spi_dev;
	struct spi_config spi_config;
#if defined(IL0323_CS_CNTRL)
	struct spi_cs_control cs_ctrl;
#endif
};

/* Border and data polarity settings */

static u8_t bdd_polarity;
static u8_t lastData[1280];
static bool blanking_on = true;


static inline int il0323_write_cmd(struct il0323_data *driver,
				   u8_t cmd, u8_t *data, size_t len)
{
	struct spi_buf buf = {.buf = &cmd, .len = sizeof(cmd)};
	struct spi_buf_set buf_set = {.buffers = &buf, .count = 1};

	gpio_pin_set(driver->dc, IL0323_DC_PIN, 0);
	if (spi_write(driver->spi_dev, &driver->spi_config, &buf_set)) {
		return -EIO;
	}
	k_sleep(IL0323_BUSY_DELAY);
	if (data != NULL) {
		buf.buf = data;
		buf.len = len;
		gpio_pin_set(driver->dc, IL0323_DC_PIN, 1);
		if (spi_write(driver->spi_dev, &driver->spi_config, &buf_set)) {
			return -EIO;
		}
	}

	return 0;
}

void il0323_full_refresh_init(struct il0323_data *driver)
{
	
	u8_t tmp[4];
	tmp[0] = 0x3f;  //Default command
	il0323_write_cmd(driver, IL0323_CMD_FACTORY, tmp, 1);
	tmp[0] = 0x6f;
	il0323_write_cmd(driver, IL0323_CMD_PSR, tmp, 1);
	tmp[0] = 0x03;
	tmp[1] = 0x00;
	tmp[2] = 0x2b;
	tmp[3] = 0x2b;
	il0323_write_cmd(driver, IL0323_CMD_PWR, tmp,4);
	tmp[0] = 0x3f;
	il0323_write_cmd(driver, IL0323_CMD_CPSET, tmp, 1);
	tmp[0] = 0x00;
	tmp[1] = 0x00;
	il0323_write_cmd(driver, IL0323_CMD_LUTOPT, tmp,2);
	tmp[0] = 0x1F;
	il0323_write_cmd(driver, IL0323_CMD_PLL, tmp, 1);
	tmp[0] = 0x57;
	il0323_write_cmd(driver, IL0323_CMD_CDI, tmp, 1);	
	tmp[0] = 0x22;
	il0323_write_cmd(driver, IL0323_CMD_TCON, tmp, 1);	
	tmp[0] = 0x50;
	tmp[1] = 0x80;
	il0323_write_cmd(driver, IL0323_CMD_TRES, tmp,2);
	tmp[0] = 0x12;
	il0323_write_cmd(driver, IL0323_CMD_VDCS, tmp, 1);
	tmp[0] = 0x33;
	il0323_write_cmd(driver, IL0323_CMD_PWS, tmp, 1);
	/*Default command*/
	il0323_write_cmd(driver, 0x23, lut_w1, 42);
	il0323_write_cmd(driver, 0x24, lut_b1, 42);
   		
}

void il0323_part_refresh_init(struct il0323_data *driver)
{
	u8_t tmp[4];
	tmp[0] = 0x3f;//Default command
	il0323_write_cmd(driver, IL0323_CMD_FACTORY, tmp, 1);
	tmp[0] = 0x6f;
	il0323_write_cmd(driver, IL0323_CMD_PSR, tmp, 1);
	tmp[0] = 0x03;
	tmp[1] = 0x00;
	tmp[2] = 0x2b;
	tmp[3] = 0x2b;
	il0323_write_cmd(driver, IL0323_CMD_PWR, tmp,4);
	tmp[0] = 0x3f;
	il0323_write_cmd(driver, IL0323_CMD_CPSET, tmp, 1);
	tmp[0] = 0x00;
	tmp[1] = 0x00;
	il0323_write_cmd(driver, IL0323_CMD_LUTOPT, tmp,2);
	tmp[0] = 0x1D;
	il0323_write_cmd(driver, IL0323_CMD_PLL, tmp, 1);
	tmp[0] = 0xF2;
	il0323_write_cmd(driver, IL0323_CMD_CDI, tmp, 1);	
	tmp[0] = 0x22;
	il0323_write_cmd(driver, IL0323_CMD_TCON, tmp, 1);
	tmp[0] = 0x00;
	il0323_write_cmd(driver, IL0323_CMD_VDCS, tmp, 1);
	tmp[0] = 0x33;
	il0323_write_cmd(driver, IL0323_CMD_PWS, tmp, 1);
	/*Default command*/
	il0323_write_cmd(driver, 0x23, lut_w, 42);
	il0323_write_cmd(driver, 0x24, lut_b, 42);
   		
}


static inline void il0323_busy_wait(struct il0323_data *driver)
{

	int pin = gpio_pin_get(driver->busy, IL0323_BUSY_PIN);

	while (pin > 0) {
		__ASSERT(pin >= 0, "Failed to get pin level");
		LOG_DBG("wait %u", pin);
		k_sleep(IL0323_BUSY_DELAY);
		pin = gpio_pin_get(driver->busy, IL0323_BUSY_PIN);
	}
}

static int il0323_update_display(const struct device *dev)
{
	struct il0323_data *driver = dev->driver_data;

	/* Turn on: booster, controller, regulators, and sensor.0x04 */
	il0323_write_cmd(driver, IL0323_CMD_PON, NULL, 0);
	il0323_busy_wait(driver);

	LOG_DBG("Trigger update sequence");
	if (il0323_write_cmd(driver, IL0323_CMD_DRF, NULL, 0)) {
		return -EIO;
	}
	il0323_busy_wait(driver);
	// il0323_write_cmd(driver, IL0323_CMD_POF, NULL, 0);
	// il0323_busy_wait(driver);

	return 0;
}



static int il0323_blanking_on(const struct device *dev)
{
	blanking_on = true;

	return 0;
}

static int il0323_write(const struct device *dev, const u16_t x, const u16_t y,
			const struct display_buffer_descriptor *desc,
			const void *buf)
{
	struct il0323_data *driver = dev->driver_data;

#ifdef IL0323_PARTIAL_REFRESH
	u16_t x_end_idx = x + desc->width - 1;
	u16_t y_end_idx = y + desc->height - 1;
	u8_t ptl[IL0323_PTL_REG_LENGTH] = {0};
	size_t buf_len;

	// Part_update_init(driver);

	LOG_DBG("x %u, y %u, height %u, width %u, pitch %u",
		x, y, desc->height, desc->width, desc->pitch);

	buf_len = MIN(desc->buf_size,
		      desc->height * desc->width / IL0323_PIXELS_PER_BYTE);
	__ASSERT(desc->width <= desc->pitch, "Pitch is smaller then width");
	__ASSERT(buf != NULL, "Buffer is not available");
	__ASSERT(buf_len != 0U, "Buffer of length zero");
	__ASSERT(!(desc->width % IL0323_PIXELS_PER_BYTE),
		 "Buffer width not multiple of %d", IL0323_PIXELS_PER_BYTE);

	if ((y_end_idx > (EPD_PANEL_HEIGHT - 1)) ||
	    (x_end_idx > (EPD_PANEL_WIDTH - 1))) {
		LOG_ERR("Position out of bounds");
		return -EINVAL;
	}
	il0323_busy_wait(driver);

	if (il0323_write_cmd(driver, IL0323_CMD_PTIN, NULL, 0)) {
		return -EIO;
	}

	/* Setup Partial Window and enable Partial Mode */
	sys_put_be16(x, &ptl[IL0323_PTL_HRST_IDX]);
	sys_put_be16(x_end_idx, &ptl[IL0323_PTL_HRED_IDX]);
	sys_put_be16(y, &ptl[IL0323_PTL_VRST_IDX]);
	sys_put_be16(y_end_idx, &ptl[IL0323_PTL_VRED_IDX]);
	ptl[4] = 0x00;
	LOG_HEXDUMP_DBG(ptl, sizeof(ptl), "ptl");
	if (il0323_write_cmd(driver, IL0323_CMD_PTL, ptl, 5)) {
		return -EIO;
	}

	if (il0323_write_cmd(driver, IL0323_CMD_DTM1, lastData, 1280)) {
		return -EIO;
	}

	if (il0323_write_cmd(driver, IL0323_CMD_DTM2, (u8_t *)buf, buf_len)) {
		return -EIO;
	}
	memcpy(lastData,(u8_t *)buf,1280);

	if (il0323_update_display(dev)) {
			return -EIO;
		}

#else

	u8_t *line;
	line = k_malloc(IL0323_NUMOF_PAGES);
	if (line == NULL) {
		return -ENOMEM;
	}
	memset(line, 0xFF, IL0323_NUMOF_PAGES);

	if (il0323_write_cmd(driver, IL0323_CMD_DTM1, line , IL0323_NUMOF_PAGES)) {
		return -EIO;
	}

	k_free(line);

	if (il0323_write_cmd(driver, IL0323_CMD_DTM2, (u8_t *)buf, buf_len)) {
		return -EIO;
	}
	if (il0323_update_display(dev)) {
		return -EIO;
	}

#endif  


	return 0;
}

static int il0323_read(const struct device *dev, const u16_t x, const u16_t y,
		       const struct display_buffer_descriptor *desc, void *buf)
{
	LOG_ERR("not supported");
	return -ENOTSUP;
}

static void *il0323_get_framebuffer(const struct device *dev)
{
	LOG_ERR("not supported");
	return NULL;
}

static int il0323_set_brightness(const struct device *dev,
				 const u8_t brightness)
{
	LOG_WRN("not supported");
	return -ENOTSUP;
}

static int il0323_set_contrast(const struct device *dev, u8_t contrast)
{
	LOG_WRN("not supported");
	return -ENOTSUP;
}

static void il0323_get_capabilities(const struct device *dev,
				    struct display_capabilities *caps)
{
	memset(caps, 0, sizeof(struct display_capabilities));
	caps->x_resolution = EPD_PANEL_WIDTH;
	caps->y_resolution = EPD_PANEL_HEIGHT;
	caps->supported_pixel_formats = PIXEL_FORMAT_MONO10;
	caps->current_pixel_format = PIXEL_FORMAT_MONO10;
	caps->screen_info = SCREEN_INFO_MONO_MSB_FIRST | SCREEN_INFO_EPD;
}

static int il0323_set_orientation(const struct device *dev,
				  const enum display_orientation
				  orientation)
{
	LOG_ERR("Unsupported");
	return -ENOTSUP;
}

static int il0323_set_pixel_format(const struct device *dev,
				   const enum display_pixel_format pf)
{
	if (pf == PIXEL_FORMAT_MONO10) {
		return 0;
	}

	LOG_ERR("not supported");
	return -ENOTSUP;
}

static int IL0323_clear_and_write_buffer(struct device *dev,
					 u8_t pattern, bool update)
{
	struct il0323_data *driver = dev->driver_data;
	u8_t *line;
	line = k_malloc(1280);
	memset(line, pattern, 1280);
	il0323_write_cmd(driver, IL0323_CMD_DTM1, line, 1280);

	memset(lastData, pattern, 1280);
	il0323_write_cmd(driver, IL0323_CMD_DTM2, line, 1280);
	k_free(line);

	if (update == true) {
		if (il0323_update_display(dev)) {
			return -EIO;
		}
	}
	blanking_on = false;
	return 0;
}
static int il0323_blanking_off(const struct device *dev)
{
	struct il0323_data *driver = dev->driver_data;

	u8_t tmp[1];
	tmp[0] = 0xf7;
	if (il0323_write_cmd(driver, IL0323_CMD_CDI, tmp , 1)) {
		return -EIO;
	}

	if (il0323_write_cmd(driver, IL0323_CMD_POF, NULL , 0)) {
		return -EIO;
	}
	il0323_busy_wait(driver);
	tmp[0] = 0xA5;//deep sleep
	if (il0323_write_cmd(driver, IL0323_CMD_DSLP, tmp , 1)) {
		return -EIO;
	}

	blanking_on = false;

	return 0;
}
static int il0323_controller_init(struct device *dev)
{
	struct il0323_data *driver = dev->driver_data;
	u8_t tmp[IL0323_TRES_REG_LENGTH];

	gpio_pin_set(driver->reset, IL0323_RESET_PIN, 0);
	k_sleep(IL0323_RESET_DELAY);
	gpio_pin_set(driver->reset, IL0323_RESET_PIN, 1);
	k_sleep(IL0323_RESET_DELAY);
	// il0323_busy_wait(driver);

	LOG_DBG("Initialize IL0323 controller");

	il0323_full_refresh_init(driver);//Required,initialize full mode first
	k_sleep(IL0323_RESET_DELAY);
	IL0323_clear_and_write_buffer(dev, 0xff, true);
#ifdef IL0323_PARTIAL_REFRESH
	k_sleep(IL0323_RESET_DELAY);
	il0323_part_refresh_init(driver);
	k_sleep(IL0323_RESET_DELAY);
#endif  /*Partial refresh init*/


	return 0;
}

static int il0323_init(struct device *dev)
{
	struct il0323_data *driver = dev->driver_data;
	LOG_DBG("");

	driver->spi_dev = device_get_binding(IL0323_BUS_NAME);
	if (driver->spi_dev == NULL) {
		LOG_ERR("Could not get SPI device for IL0323");
		return -EIO;
	}

	driver->spi_config.frequency = IL0323_SPI_FREQ;
	driver->spi_config.operation = SPI_OP_MODE_MASTER | SPI_WORD_SET(8);
	driver->spi_config.slave = DT_INST_REG_ADDR(0);
	driver->spi_config.cs = NULL;

	driver->reset = device_get_binding(IL0323_RESET_CNTRL);
	if (driver->reset == NULL) {
		LOG_ERR("Could not get GPIO port for IL0323 reset");
		return -EIO;
	}

	gpio_pin_configure(driver->reset, IL0323_RESET_PIN,
			   GPIO_OUTPUT_INACTIVE | IL0323_RESET_FLAGS);

	driver->dc = device_get_binding(IL0323_DC_CNTRL);
	if (driver->dc == NULL) {
		LOG_ERR("Could not get GPIO port for IL0323 DC signal");
		return -EIO;
	}

	gpio_pin_configure(driver->dc, IL0323_DC_PIN,
			   GPIO_OUTPUT_INACTIVE | IL0323_DC_FLAGS);

	driver->busy = device_get_binding(IL0323_BUSY_CNTRL);
	if (driver->busy == NULL) {
		LOG_ERR("Could not get GPIO port for IL0323 busy signal");
		return -EIO;
	}

	gpio_pin_configure(driver->busy, IL0323_BUSY_PIN,
			   GPIO_INPUT | IL0323_BUSY_FLAGS);

#if defined(IL0323_CS_CNTRL)
	driver->cs_ctrl.gpio_dev = device_get_binding(IL0323_CS_CNTRL);
	if (!driver->cs_ctrl.gpio_dev) {
		LOG_ERR("Unable to get SPI GPIO CS device");
		return -EIO;
	}

	driver->cs_ctrl.gpio_pin = IL0323_CS_PIN;
	driver->cs_ctrl.delay = 0U;
	driver->spi_config.cs = &driver->cs_ctrl;
#endif

	return il0323_controller_init(dev);
}

static struct il0323_data il0323_driver;

static struct display_driver_api il0323_driver_api = {
	.blanking_on = il0323_blanking_on,
	.blanking_off = il0323_blanking_off,
	.write = il0323_write,
	.read = il0323_read,
	.get_framebuffer = il0323_get_framebuffer,
	.set_brightness = il0323_set_brightness,
	.set_contrast = il0323_set_contrast,
	.get_capabilities = il0323_get_capabilities,
	.set_pixel_format = il0323_set_pixel_format,
	.set_orientation = il0323_set_orientation,
};


DEVICE_AND_API_INIT(IL0323, DT_INST_LABEL(0), il0323_init,
		    &il0323_driver, NULL,
		    POST_KERNEL, CONFIG_APPLICATION_INIT_PRIORITY,
		    &il0323_driver_api);
