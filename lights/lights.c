//
// Copyright (C) 2025 The LineageOS Project
//
// SPDX-License-Identifier: Apache-2.0
//

#define LOG_TAG "lights"

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <sys/ioctl.h>
#include <sys/types.h>

#include <cutils/log.h>
#include <hardware/lights.h>

#include "lights.h"

static pthread_once_t g_init = PTHREAD_ONCE_INIT;
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;

struct led leds[NUM_LEDS] = {
    [RED_LED] = {
        .brightness = {"/sys/class/leds/red/brightness", -1},
        .high_time = {"/sys/class/leds/red_bl/high_time", -1},
        .low_time = {"/sys/class/leds/red_bl/low_time", -1},
        .rising_time = {"/sys/class/leds/red_bl/rising_time", -1},
        .falling_time = {"/sys/class/leds/red_bl/falling_time", -1},
        .on_off = {"/sys/class/leds/red_bl/on_off", -1},
    },
    [GREEN_LED] = {
        .brightness = {"/sys/class/leds/green/brightness", -1},
        .high_time = {"/sys/class/leds/green_bl/high_time", -1},
        .low_time = {"/sys/class/leds/green_bl/low_time", -1},
        .rising_time = {"/sys/class/leds/green_bl/rising_time", -1},
        .falling_time = {"/sys/class/leds/green_bl/falling_time", -1},
        .on_off = {"/sys/class/leds/green_bl/on_off", -1},
    },
    [BLUE_LED] = {
        .brightness = {"/sys/class/leds/blue/brightness", -1},
        .high_time = {"/sys/class/leds/blue_bl/high_time", -1},
        .low_time = {"/sys/class/leds/blue_bl/low_time", -1},
        .rising_time = {"/sys/class/leds/blue_bl/rising_time", -1},
        .falling_time = {"/sys/class/leds/blue_bl/falling_time", -1},
        .on_off = {"/sys/class/leds/blue_bl/on_off", -1},
    },
    [LCD_BACKLIGHT] = {
        .brightness = {"/sys/class/backlight/sprd_backlight/brightness", -1},
    },
    [BUTTONS_LED] = {
        .brightness = {"/sys/class/leds/keyboard-backlight/brightness", -1},
    },
};

static int rgb_to_brightness(const struct light_state_t* state) {
    int color = state->color & 0x00FFFFFF;
    return ((77 * ((color >> 16) & 0x00FF)) +
            (150 * ((color >> 8) & 0x00FF)) +
            (29 * (color & 0x00FF))) >> 8;
}

static int is_lit(const struct light_state_t* state) {
    return state->color & 0x00FFFFFF;
}

void init_g_lock(void) {
    pthread_mutex_init(&g_lock, NULL);
}

static int init_prop(struct led_prop* prop) {
    if (!prop->filename) return 0;
    prop->fd = open(prop->filename, O_RDWR);
    if (prop->fd < 0) {
        ALOGE("Failed to open %s: %s", prop->filename, strerror(errno));
        return -errno;
    }
    return 0;
}

static void close_prop(struct led_prop* prop) {
    if (prop->fd >= 0) {
        close(prop->fd);
        prop->fd = -1;
    }
}

void init_globals(void) {
    for (int i = 0; i < NUM_LEDS; ++i) {
        init_prop(&leds[i].brightness);
        init_prop(&leds[i].high_time);
        init_prop(&leds[i].low_time);
        init_prop(&leds[i].rising_time);
        init_prop(&leds[i].falling_time);
        init_prop(&leds[i].on_off);
    }
}

static int write_int(struct led_prop* prop, int value) {
    if (!prop->filename) return -EINVAL;

    int fd = open(prop->filename, O_RDWR);
    if (fd < 0) {
        ALOGE("Failed to write to %s: %s", prop->filename, strerror(errno));
        return -errno;
    }

    char buffer[20];
    int bytes = snprintf(buffer, sizeof(buffer), "%d\n", value);
    int amt = write(fd, buffer, bytes);
    close(fd);

    return (amt == -1) ? -errno : 0;
}

static int set_light_backlight(struct light_device_t* dev, const struct light_state_t* state) {
    int brightness = rgb_to_brightness(state);
    pthread_mutex_lock(&g_lock);
    int err = write_int(&leds[LCD_BACKLIGHT].brightness, brightness);
    pthread_mutex_unlock(&g_lock);
    return err;
}

static int set_light_buttons(struct light_device_t* dev, const struct light_state_t* state) {
    int on = is_lit(state);
    pthread_mutex_lock(&g_lock);
    int err = write_int(&leds[BUTTONS_LED].brightness, on ? 255 : 0);
    pthread_mutex_unlock(&g_lock);
    return err;
}

static int set_light_keyboard(struct light_device_t* dev, const struct light_state_t* state) {
    return set_light_buttons(dev, state);
}

static void configure_led(struct led* led, int rise, int high, int fall, int low, int on) {
    write_int(&led->rising_time, rise);
    write_int(&led->high_time, high);
    write_int(&led->falling_time, fall);
    write_int(&led->low_time, low);
    write_int(&led->on_off, on);
}

static int close_lights(struct light_device_t* dev) {
    int i;

    for (i = 0; i < NUM_LEDS; ++i) {
        close_prop(&leds[i].brightness);
        close_prop(&leds[i].high_time);
        close_prop(&leds[i].low_time);
        close_prop(&leds[i].rising_time);
        close_prop(&leds[i].falling_time);
        close_prop(&leds[i].on_off);
    }

    if (dev) free(dev);
    return 0;
}

static int set_breath_light(struct light_device_t* dev, const struct light_state_t* state) {
    unsigned int colorRGB = state->color & 0xFFFFFF;
    unsigned int ms_unit = 125;

    int rise = (state->flashOnMS / ms_unit) * 2 / 3;
    int high = (state->flashOnMS / ms_unit) / 3;
    int fall = (state->flashOffMS / ms_unit) / 5;
    int low = (state->flashOffMS / ms_unit) * 4 / 5;

    pthread_mutex_lock(&g_lock);
    if (colorRGB) {
        if (colorRGB & 0xFF0000) configure_led(&leds[RED_LED], rise, high, fall, low, 1);
        if (colorRGB & 0x00FF00) configure_led(&leds[GREEN_LED], rise, high, fall, low, 1);
        if (colorRGB & 0x0000FF) configure_led(&leds[BLUE_LED], rise, high, fall, low, 1);
    }
    pthread_mutex_unlock(&g_lock);
    return 0;
}

static int set_light_leds_notifications(struct light_device_t* dev,
                                        struct light_state_t const* state) {
    set_breath_light(dev, state);
    return -EINVAL;
}

static int set_light_leds_attention(struct light_device_t* dev, struct light_state_t const* state) {
    return -EINVAL;
}

static int open_lights(const struct hw_module_t* module, const char* name, struct hw_device_t** device) {
    int (*set_light)(struct light_device_t*, const struct light_state_t*);

    if (0 == strcmp(LIGHT_ID_BACKLIGHT, name))
        set_light = set_light_backlight;
    else if (0 == strcmp(LIGHT_ID_KEYBOARD, name))
        set_light = set_light_keyboard;
    else if (0 == strcmp(LIGHT_ID_BUTTONS, name))
        set_light = set_light_buttons;
    else
        return -EINVAL;

    pthread_once(&g_init, init_g_lock);

    struct light_device_t* dev = malloc(sizeof(struct light_device_t));
    if (!dev) {
        ALOGE("Failed to allocate memory for light device");
        return -ENOMEM;
    }

    memset(dev, 0, sizeof(*dev));

    dev->common.tag = HARDWARE_DEVICE_TAG;
    dev->common.version = 0;
    dev->common.module = (struct hw_module_t*)module;
    dev->common.close = (int (*)(struct hw_device_t*))free;
    dev->set_light = set_light;

    *device = (struct hw_device_t*)dev;

    return 0;
}

static struct hw_module_methods_t lights_module_methods = {
    .open = open_lights,
};

struct hw_module_t HAL_MODULE_INFO_SYM = {
    .tag = HARDWARE_MODULE_TAG,
    .version_major = 1,
    .version_minor = 0,
    .id = LIGHTS_HARDWARE_MODULE_ID,
    .name = "SPRD Lights Module",
    .author = "R0rt1z2",
    .methods = &lights_module_methods,
};
