//
// Copyright (C) 2025 The LineageOS Project
//
// SPDX-License-Identifier: Apache-2.0
//

#pragma once

struct led_prop {
    const char* filename;
    int fd;
};

struct led {
    struct led_prop brightness;
    struct led_prop high_time;
    struct led_prop low_time;
    struct led_prop rising_time;
    struct led_prop falling_time;
    struct led_prop on_off;
};

enum {
    RED_LED,
    GREEN_LED,
    BLUE_LED,
    LCD_BACKLIGHT,
    BUTTONS_LED,
    NUM_LEDS,
};