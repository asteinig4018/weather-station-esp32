#pragma once
/**
 * board.h — Hardware pin assignments for the custom ESP32-S3 weather station.
 *
 * Chip   : ESP32-S3-WROOM-1-N8 (8 MB flash, dual-core Xtensa LX7)
 * Display: NHD-2.4-240320CF-CTXI#-F (ILI9341, 8-bit Intel 8080 parallel)
 * IMU    : ISM330DHC (ST, 6-axis accel+gyro) on I2C
 * Baro   : BMP851 (Bosch, pressure + temperature) on same I2C bus
 * Air    : SEN66 (Sensirion, PM / VOC / NOx / T / RH) on I2C
 * Power  : USB or Li-Ion battery, PWRGD line signals valid supply
 *
 * TODO: Fill in actual GPIO numbers once PCB routing is confirmed.
 *       All values marked -1 are unassigned placeholders.
 */

/* =========================================================================
 * I2C bus (shared by ISM330DHC, BMP851, SEN66)
 * ========================================================================= */
#define BOARD_I2C_PORT      0           /*!< I2C peripheral number (0 or 1) */
#define BOARD_I2C_SDA       GPIO_NUM_8  /*!< TODO: confirm pin */
#define BOARD_I2C_SCL       GPIO_NUM_9  /*!< TODO: confirm pin */
#define BOARD_I2C_FREQ_HZ   400000      /*!< 400 kHz fast-mode */

/* I2C device addresses */
#define BOARD_ADDR_ISM330DHC  0x6A  /*!< SDO/SA0 tied LOW → 0x6A */
#define BOARD_ADDR_BMP851     0x76  /*!< SDO tied LOW → 0x76 */
#define BOARD_ADDR_SEN66      0x6B  /*!< Fixed by Sensirion */

/* =========================================================================
 * Display — 8-bit Intel 8080 parallel (ILI9341, 240 × 320)
 * ========================================================================= */
#define BOARD_LCD_WIDTH     240
#define BOARD_LCD_HEIGHT    320

/* Data bus D[7:0] — must map to contiguous or any 8 GPIOs */
#define BOARD_LCD_D0        GPIO_NUM_1  /*!< TODO */
#define BOARD_LCD_D1        GPIO_NUM_2  /*!< TODO */
#define BOARD_LCD_D2        GPIO_NUM_3  /*!< TODO */
#define BOARD_LCD_D3        GPIO_NUM_4  /*!< TODO */
#define BOARD_LCD_D4        GPIO_NUM_5  /*!< TODO */
#define BOARD_LCD_D5        GPIO_NUM_6  /*!< TODO */
#define BOARD_LCD_D6        GPIO_NUM_7  /*!< TODO */
#define BOARD_LCD_D7        GPIO_NUM_15 /*!< TODO */

/* Control signals */
#define BOARD_LCD_WR        GPIO_NUM_16 /*!< Write strobe (/WR) */
#define BOARD_LCD_RD        GPIO_NUM_17 /*!< Read strobe (/RD), tie HIGH if read unused */
#define BOARD_LCD_DC        GPIO_NUM_18 /*!< Data/Command (RS) */
#define BOARD_LCD_CS        GPIO_NUM_10 /*!< Chip select, active LOW */
#define BOARD_LCD_RST       GPIO_NUM_11 /*!< Hardware reset, active LOW */
#define BOARD_LCD_BL        GPIO_NUM_12 /*!< Backlight enable (HIGH = on, or PWM) */

/* I80 bus clock — keep ≤ 20 MHz for reliable parallel operation */
#define BOARD_LCD_PCLK_HZ   (10 * 1000 * 1000)

/* =========================================================================
 * Buttons
 *   UX_BTN  : primary user-facing button
 *   DBG_BTN0: debug/boot helper 0
 *   DBG_BTN1: debug/boot helper 1
 * All buttons assumed active LOW with internal pull-up.
 * ========================================================================= */
#define BOARD_BTN_UX        GPIO_NUM_0  /*!< UX button (often BOOT pin) */
#define BOARD_BTN_DBG0      GPIO_NUM_35 /*!< TODO */
#define BOARD_BTN_DBG1      GPIO_NUM_36 /*!< TODO */

/* =========================================================================
 * Switches  (active LOW, internal pull-up assumed)
 * ========================================================================= */
#define BOARD_SW0           GPIO_NUM_37 /*!< TODO */
#define BOARD_SW1           GPIO_NUM_38 /*!< TODO */

/* =========================================================================
 * LEDs  (active HIGH assumed; invert logic if active LOW)
 * ========================================================================= */
#define BOARD_LED_STATUS    GPIO_NUM_39 /*!< Status / power LED */
#define BOARD_LED_ALERT     GPIO_NUM_40 /*!< Alert / warning LED */

/* =========================================================================
 * Power
 * ========================================================================= */
/** PWRGD from power management circuit; HIGH = supply voltage valid. */
#define BOARD_PWRGD         GPIO_NUM_41 /*!< TODO */
