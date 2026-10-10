// ESP PLANTS board configuration for Waveshare ESP32-S3-Touch-LCD-7B.
// Geometry/timings follow Waveshare's official 1024x600 7B Arduino example.
// 7B display behavior has been exercised on hardware; release validation remains separate.
#pragma once

#define I2C_MASTER_SCL_IO 9
#define I2C_MASTER_SDA_IO 8
#define I2C_MASTER_NUM I2C_NUM_0
#define I2C_MASTER_FREQ_HZ 400000
#define I2C_MASTER_TX_BUF_DISABLE 0
#define I2C_MASTER_RX_BUF_DISABLE 0
#define I2C_MASTER_TIMEOUT_MS 1000

#define GPIO_INPUT_IO_4 4
#define GPIO_INPUT_PIN_SEL (1ULL << GPIO_INPUT_IO_4)

#define ESP_PANEL_USE_CUSTOM_BOARD (1)

#if ESP_PANEL_USE_CUSTOM_BOARD
#define ESP_PANEL_USE_LCD (1)

#if ESP_PANEL_USE_LCD
#define ESP_PANEL_LCD_NAME ST7262
#define ESP_PANEL_LCD_WIDTH (1024)
#define ESP_PANEL_LCD_HEIGHT (600)
#define ESP_PANEL_LCD_BUS_SKIP_INIT_HOST (1)
#define ESP_PANEL_LCD_BUS_TYPE (ESP_PANEL_BUS_TYPE_RGB)

#define ESP_PANEL_LCD_RGB_CLK_HZ (24 * 1000 * 1000)
#define ESP_PANEL_LCD_RGB_HPW (162)
#define ESP_PANEL_LCD_RGB_HBP (152)
#define ESP_PANEL_LCD_RGB_HFP (48)
#define ESP_PANEL_LCD_RGB_VPW (45)
#define ESP_PANEL_LCD_RGB_VBP (13)
#define ESP_PANEL_LCD_RGB_VFP (3)
#define ESP_PANEL_LCD_RGB_PCLK_ACTIVE_NEG (1)
#define ESP_PANEL_LCD_RGB_DATA_WIDTH (16)
#define ESP_PANEL_LCD_RGB_PIXEL_BITS (16)

// Board-level fallback only: lcd_init() overrides this *before panel->begin()*
// with LVGL_PORT_DISP_BUFFER_NUM = 3 for the active triple-buffer/full-refresh
// path (3 x 1024 x 600 x RGB565 = 3,686,400 bytes of PSRAM). Do not treat this
// fallback define as the effective framebuffer count or change runtime buffers.
#define ESP_PANEL_LCD_RGB_FRAME_BUF_NUM (1)
// VSYNC-restart experiment baseline: retain the tested 24 MHz 7B pixel
// clock and 15-line bounce buffer while changing only the linked IDF driver.
#define ESP_PANEL_LCD_RGB_BOUNCE_BUF_SIZE (ESP_PANEL_LCD_WIDTH * 15)

#define ESP_PANEL_LCD_RGB_IO_HSYNC (46)
#define ESP_PANEL_LCD_RGB_IO_VSYNC (3)
#define ESP_PANEL_LCD_RGB_IO_DE (5)
#define ESP_PANEL_LCD_RGB_IO_PCLK (7)
#define ESP_PANEL_LCD_RGB_IO_DISP (-1)
#define ESP_PANEL_LCD_RGB_IO_DATA0 (14)
#define ESP_PANEL_LCD_RGB_IO_DATA1 (38)
#define ESP_PANEL_LCD_RGB_IO_DATA2 (18)
#define ESP_PANEL_LCD_RGB_IO_DATA3 (17)
#define ESP_PANEL_LCD_RGB_IO_DATA4 (10)
#define ESP_PANEL_LCD_RGB_IO_DATA5 (39)
#define ESP_PANEL_LCD_RGB_IO_DATA6 (0)
#define ESP_PANEL_LCD_RGB_IO_DATA7 (45)
#define ESP_PANEL_LCD_RGB_IO_DATA8 (48)
#define ESP_PANEL_LCD_RGB_IO_DATA9 (47)
#define ESP_PANEL_LCD_RGB_IO_DATA10 (21)
#define ESP_PANEL_LCD_RGB_IO_DATA11 (1)
#define ESP_PANEL_LCD_RGB_IO_DATA12 (2)
#define ESP_PANEL_LCD_RGB_IO_DATA13 (42)
#define ESP_PANEL_LCD_RGB_IO_DATA14 (41)
#define ESP_PANEL_LCD_RGB_IO_DATA15 (40)

#define ESP_PANEL_LCD_COLOR_BITS (16)
#define ESP_PANEL_LCD_BGR_ORDER (0)
#define ESP_PANEL_LCD_INEVRT_COLOR (0)
#define ESP_PANEL_LCD_SWAP_XY (0)
#define ESP_PANEL_LCD_MIRROR_X (0)
#define ESP_PANEL_LCD_MIRROR_Y (0)
#define ESP_PANEL_LCD_IO_RST (-1)
#define ESP_PANEL_LCD_RST_LEVEL (0)
#endif

#define ESP_PANEL_USE_TOUCH (1)
#if ESP_PANEL_USE_TOUCH
#define ESP_PANEL_TOUCH_NAME GT911
#define ESP_PANEL_TOUCH_H_RES (ESP_PANEL_LCD_WIDTH)
#define ESP_PANEL_TOUCH_V_RES (ESP_PANEL_LCD_HEIGHT)
#define ESP_PANEL_TOUCH_BUS_SKIP_INIT_HOST (1)
#define ESP_PANEL_TOUCH_BUS_TYPE (ESP_PANEL_BUS_TYPE_I2C)
#define ESP_PANEL_TOUCH_BUS_HOST_ID (0)
#define ESP_PANEL_TOUCH_I2C_ADDRESS (0)
#define ESP_PANEL_TOUCH_SWAP_XY (0)
#define ESP_PANEL_TOUCH_MIRROR_X (0)
#define ESP_PANEL_TOUCH_MIRROR_Y (0)
#define ESP_PANEL_TOUCH_IO_RST (-1)
#define ESP_PANEL_TOUCH_RST_LEVEL (0)
#define ESP_PANEL_TOUCH_IO_INT (-1)
#define ESP_PANEL_TOUCH_INT_LEVEL (0)
#endif

// The 7B uses Waveshare's I2C IO-extension MCU at address 0x24.
// ESP PLANTS initializes it directly because its register protocol is not the
// CH422G protocol used by the original 800x480 board.
#define ESP_PLANTS_7B_IO_EXTENSION_ADDR (0x24)
#define ESP_PLANTS_7B_IO_MODE_REG (0x02)
#define ESP_PLANTS_7B_IO_OUTPUT_REG (0x03)
#define ESP_PLANTS_7B_IO_PWM_REG (0x05)
#define ESP_PLANTS_7B_TOUCH_RESET_IO (1)
#define ESP_PLANTS_7B_BACKLIGHT_IO (2)
#define ESP_PLANTS_7B_LCD_RESET_IO (3)
// EXIO5 selects the ESP32-S3 GPIO19/20 routing: LOW keeps native USB,
// HIGH selects CAN. ESP PLANTS uses native USB; UART2 remains dedicated to H2.
#define ESP_PLANTS_7B_USB_CAN_SEL_IO (5)
#define ESP_PLANTS_7B_DEFAULT_BRIGHTNESS_LEVEL (5)

// Backlight/reset are handled explicitly in Waveshare_ST7262_LVGL.cpp.
#define ESP_PANEL_USE_BACKLIGHT (0)
#define ESP_PANEL_USE_EXPANDER (0)

#define ESP_PANEL_BOARD_CUSTOM_FILE_VERSION_MAJOR 0
#define ESP_PANEL_BOARD_CUSTOM_FILE_VERSION_MINOR 2
#define ESP_PANEL_BOARD_CUSTOM_FILE_VERSION_PATCH 2
#endif
