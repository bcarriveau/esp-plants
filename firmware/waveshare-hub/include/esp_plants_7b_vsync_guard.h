#pragma once

#ifdef ESP_PLANTS_WAVESHARE_7B
#include "sdkconfig.h"

#if !defined(CONFIG_LCD_RGB_RESTART_IN_VSYNC) || !CONFIG_LCD_RGB_RESTART_IN_VSYNC
#error "ESP PLANTS 7B VSYNC SDK missing CONFIG_LCD_RGB_RESTART_IN_VSYNC"
#endif

#if !defined(CONFIG_GDMA_CTRL_FUNC_IN_IRAM) || !CONFIG_GDMA_CTRL_FUNC_IN_IRAM
#error "ESP PLANTS 7B VSYNC SDK missing CONFIG_GDMA_CTRL_FUNC_IN_IRAM"
#endif

#if !defined(CONFIG_COMPILER_OPTIMIZATION_PERF) || !CONFIG_COMPILER_OPTIMIZATION_PERF
#error "ESP PLANTS 7B VSYNC SDK is not using the 3.0.7-h performance optimization config"
#endif

#if !defined(CONFIG_ESP32S3_DATA_CACHE_LINE_64B) || !CONFIG_ESP32S3_DATA_CACHE_LINE_64B
#error "ESP PLANTS 7B VSYNC SDK is not using the 3.0.7-h 64-byte S3 data cache line"
#endif

#if !defined(CONFIG_SPIRAM_FETCH_INSTRUCTIONS) || !CONFIG_SPIRAM_FETCH_INSTRUCTIONS
#error "ESP PLANTS 7B VSYNC SDK is not using 3.0.7-h PSRAM instruction XIP"
#endif

#if !defined(CONFIG_SPIRAM_RODATA) || !CONFIG_SPIRAM_RODATA
#error "ESP PLANTS 7B VSYNC SDK is not using 3.0.7-h PSRAM rodata XIP"
#endif
#endif
