/*
 * Copyright (c) 2026
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/led_strip.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

#include <drivers/ext_power.h>

#include <zmk/event_manager.h>
#include <zmk/events/hid_indicators_changed.h>
#include <zmk/workqueue.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#define LED_CLCK BIT(1)

#if !DT_HAS_CHOSEN(zmk_underglow)
#error "A zmk,underglow chosen node must be declared"
#endif

#define STRIP_NODE DT_CHOSEN(zmk_underglow)
#define STRIP_NUM_PIXELS DT_PROP(STRIP_NODE, chain_length)

BUILD_ASSERT(CONFIG_EYESLASH_CORNE_CAPS_LOCK_LED_INDEX >= 0,
             "Caps lock LED index must be non-negative");
BUILD_ASSERT(CONFIG_EYESLASH_CORNE_CAPS_LOCK_LED_INDEX < STRIP_NUM_PIXELS,
             "Caps lock LED index must be within the LED strip length");
BUILD_ASSERT(CONFIG_EYESLASH_CORNE_CAPS_LOCK_LED_BRIGHTNESS >= 0 &&
                 CONFIG_EYESLASH_CORNE_CAPS_LOCK_LED_BRIGHTNESS <= 255,
             "Caps lock LED brightness must be between 0 and 255");

static const struct device *const led_strip = DEVICE_DT_GET(STRIP_NODE);

#if IS_ENABLED(CONFIG_ZMK_RGB_UNDERGLOW_EXT_POWER)
static const struct device *const ext_power = DEVICE_DT_GET(DT_INST(0, zmk_ext_power_generic));
#endif

static struct led_rgb pixels[STRIP_NUM_PIXELS];
static bool caps_lock_on;
static struct k_work caps_lock_led_work;

static void caps_lock_led_apply(struct k_work *work) {
    ARG_UNUSED(work);

    if (!device_is_ready(led_strip)) {
        LOG_WRN("Caps lock LED strip is not ready");
        return;
    }

#if IS_ENABLED(CONFIG_ZMK_RGB_UNDERGLOW_EXT_POWER)
    if (caps_lock_on && device_is_ready(ext_power)) {
        int err = ext_power_enable(ext_power);
        if (err < 0) {
            LOG_WRN("Failed to enable external power for caps lock LED (%d)", err);
        }
    }
#endif

    for (int i = 0; i < STRIP_NUM_PIXELS; i++) {
        pixels[i] = (struct led_rgb){.r = 0, .g = 0, .b = 0};
    }

    if (caps_lock_on) {
        pixels[CONFIG_EYESLASH_CORNE_CAPS_LOCK_LED_INDEX] =
            (struct led_rgb){.r = CONFIG_EYESLASH_CORNE_CAPS_LOCK_LED_BRIGHTNESS, .g = 0, .b = 0};
    }

    int err = led_strip_update_rgb(led_strip, pixels, STRIP_NUM_PIXELS);
    if (err < 0) {
        LOG_WRN("Failed to update caps lock LED (%d)", err);
    }
}

static int caps_lock_led_listener(const zmk_event_t *eh) {
    const struct zmk_hid_indicators_changed *ev = as_zmk_hid_indicators_changed(eh);

    if (ev == NULL) {
        return ZMK_EV_EVENT_BUBBLE;
    }

    caps_lock_on = (ev->indicators & LED_CLCK) != 0;
    k_work_submit_to_queue(zmk_workqueue_lowprio_work_q(), &caps_lock_led_work);

    return ZMK_EV_EVENT_BUBBLE;
}

static int caps_lock_led_init(void) {
    k_work_init(&caps_lock_led_work, caps_lock_led_apply);
    return 0;
}

SYS_INIT(caps_lock_led_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);

ZMK_LISTENER(caps_lock_led, caps_lock_led_listener);
ZMK_SUBSCRIPTION(caps_lock_led, zmk_hid_indicators_changed);
