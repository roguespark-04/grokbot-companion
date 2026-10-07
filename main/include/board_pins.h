/**
 * @file board_pins.h
 * @brief Pin / bus map for Waveshare ESP32-S3-Touch-AMOLED-1.75
 *
 * Source of truth:
 *   https://github.com/waveshareteam/ESP32-S3-Touch-AMOLED-1.75/blob/main/HARDWARE_REFERENCE.md
 *   BSP: firmware/brookesia/components/waveshare__esp32_s3_touch_amoled_1_75/include/bsp/...
 *
 * Prefer pasting / linking Waveshare BSP examples rather than re-deriving pins.
 * When adopting the official BSP package, these macros should match BSP_* names.
 */
#pragma once

#include "driver/gpio.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Shared I2C (SCL/SDA) ---- */
#define MUSE_I2C_SCL            GPIO_NUM_14
#define MUSE_I2C_SDA            GPIO_NUM_15
#define MUSE_I2C_FREQ_HZ        400000

/* I2C 7-bit addresses */
#define MUSE_I2C_ADDR_ES8311    0x18   /* playback codec */
#define MUSE_I2C_ADDR_TCA9554   0x20   /* IO expander */
#define MUSE_I2C_ADDR_AXP2101   0x34   /* PMIC */
#define MUSE_I2C_ADDR_ES7210    0x40   /* mic / AEC ADC */
#define MUSE_I2C_ADDR_PCF85063  0x51   /* RTC */
#define MUSE_I2C_ADDR_CST9217   0x5A   /* touch */
#define MUSE_I2C_ADDR_QMI8658   0x6B   /* IMU */

/* ---- I2S (shared clocks: ES8311 TX + ES7210 TDM RX) ---- */
#define MUSE_I2S_MCLK           GPIO_NUM_42
#define MUSE_I2S_BCLK           GPIO_NUM_9
#define MUSE_I2S_LRCK           GPIO_NUM_45
#define MUSE_I2S_DOUT           GPIO_NUM_8    /* ESP → ES8311 DSDIN (playback) */
#define MUSE_I2S_DIN            GPIO_NUM_10   /* ES7210 → ESP (capture TDM) */
#define MUSE_PA_CTRL            GPIO_NUM_46   /* NS4150B amp enable — prefer BSP API */

/* ---- Display CO5300 (QSPI) ---- */
#define MUSE_LCD_CS             GPIO_NUM_12
#define MUSE_LCD_PCLK           GPIO_NUM_38
#define MUSE_LCD_DATA0          GPIO_NUM_4
#define MUSE_LCD_DATA1          GPIO_NUM_5
#define MUSE_LCD_DATA2          GPIO_NUM_6
#define MUSE_LCD_DATA3          GPIO_NUM_7
#define MUSE_LCD_RST            GPIO_NUM_39
#define MUSE_LCD_TE             GPIO_NUM_13
/* No GPIO backlight — brightness via CO5300 panel commands */
#define MUSE_LCD_WIDTH          466
#define MUSE_LCD_HEIGHT         466

/* ---- Touch CST9217 ---- */
#define MUSE_TP_RST             GPIO_NUM_40
#define MUSE_TP_INT             GPIO_NUM_11

/* ---- TF / microSD (1-bit SDMMC in Waveshare BSP) ---- */
#define MUSE_SD_CMD             GPIO_NUM_1
#define MUSE_SD_CLK             GPIO_NUM_2
#define MUSE_SD_D0              GPIO_NUM_3
#define MUSE_SD_D3_CS           GPIO_NUM_41   /* wired; unused in 1-bit SDMMC */

/* ---- Buttons (confirmed) ---- */
#define MUSE_BOOT_BUTTON        GPIO_NUM_0    /* active low; press-and-hold PTT */
/* PWR = power on/off only (AXP2101). SYS_OUT on TCA9554 EXIO4 (high=pressed).
 * Not used for PTT — leave power management to the board/PMIC. */

/* ---- Expansion header H2 (2.54 mm 8-pin) ---- */
/* 1=VBUS 2=GND 3=3V3 4=GPIO44/U0RXD 5=GPIO43/U0TXD 6=GPIO17 7=GPIO18 8=GPIO16 */

#ifdef __cplusplus
}
#endif
