/* SPDX-License-Identifier: MIT */
#pragma once
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "layer-colors.h"
#define CONFIG_RGBLED_WIDGET 1
#if defined(RGB_WIDGET_LAYER_PULSE_TEST)
#define CONFIG_RGBLED_WIDGET_SHOW_CONNECTION_STATUS 0
#define CONFIG_RGBLED_WIDGET_SHOW_BATTERY_STATUS 0
#define CONFIG_RGBLED_WIDGET_LAYER_SINGLE_PULSE 1
#define CONFIG_RGBLED_WIDGET_SHOW_LAYER_CHANGE 1
#else
#define CONFIG_RGBLED_WIDGET_SHOW_CONNECTION_STATUS 1
#define CONFIG_RGBLED_WIDGET_SHOW_BATTERY_STATUS 1
#define CONFIG_RGBLED_WIDGET_LAYER_SINGLE_PULSE 0
#define CONFIG_RGBLED_WIDGET_SHOW_LAYER_COLORS 1
#endif
#if defined(RGB_WIDGET_IGNORE_AML_TEST)
#define CONFIG_RGBLED_WIDGET_LAYER_IGNORE_MASK 0x200
#else
#define CONFIG_RGBLED_WIDGET_LAYER_IGNORE_MASK 0
#endif
#define CONFIG_ZMK_USB 1
#define CONFIG_ZMK_BLE 1
#define CONFIG_ZMK_BATTERY_REPORTING 1
#define CONFIG_RGBLED_WIDGET_CONN_SHOW_USB 1
#define CONFIG_RGBLED_WIDGET_BATTERY_SHOW_SELF 1
#if defined(RGB_WIDGET_LAYER_PULSE_TEST)
#define CONFIG_RGBLED_WIDGET_INTERVAL_MS 0
#else
#define CONFIG_RGBLED_WIDGET_INTERVAL_MS 20
#endif
#define CONFIG_RGBLED_WIDGET_CONN_BLINK_MS 80
#define CONFIG_RGBLED_WIDGET_BATTERY_BLINK_MS 80
#define CONFIG_RGBLED_WIDGET_BATTERY_LEVEL_HIGH 80
#define CONFIG_RGBLED_WIDGET_BATTERY_LEVEL_LOW 20
#define CONFIG_RGBLED_WIDGET_BATTERY_LEVEL_CRITICAL 5
#define CONFIG_RGBLED_WIDGET_BATTERY_COLOR_HIGH 2
#define CONFIG_RGBLED_WIDGET_BATTERY_COLOR_MEDIUM 3
#define CONFIG_RGBLED_WIDGET_BATTERY_COLOR_LOW 1
#define CONFIG_RGBLED_WIDGET_BATTERY_COLOR_CRITICAL 1
#define CONFIG_RGBLED_WIDGET_BATTERY_COLOR_MISSING 5
#define CONFIG_RGBLED_WIDGET_CONN_COLOR_USB 6
#define CONFIG_RGBLED_WIDGET_CONN_COLOR_CONNECTED 4
#define CONFIG_RGBLED_WIDGET_CONN_COLOR_ADVERTISING 3
#define CONFIG_RGBLED_WIDGET_CONN_COLOR_DISCONNECTED 1
#if defined(RGB_WIDGET_IGNORE_AML_TEST)
#define CONFIG_RGBLED_WIDGET_LAYER_BLINK_MS 1000
#else
#define CONFIG_RGBLED_WIDGET_LAYER_BLINK_MS 10
#endif
#define CONFIG_RGBLED_WIDGET_LAYER_COLOR 6
#define CONFIG_RGBLED_WIDGET_LAYER_DEBOUNCE_MS 10

/* Only ZMK inputs/events and behavior registration are controlled. Kernel,
 * queue, threads, mutexes, LED driver and GPIO emulator are real Zephyr. */
enum zmk_transport { ZMK_TRANSPORT_NONE, ZMK_TRANSPORT_USB, ZMK_TRANSPORT_BLE };
struct zmk_endpoint_instance { enum zmk_transport transport; };
static inline struct zmk_endpoint_instance zmk_endpoint_get_selected(void) {
    return (struct zmk_endpoint_instance){ZMK_TRANSPORT_BLE};
}
static inline enum zmk_transport zmk_endpoint_get_preferred_transport(void) { return ZMK_TRANSPORT_BLE; }
static inline uint8_t zmk_ble_active_profile_index(void) { return 0; }
static inline bool zmk_ble_active_profile_is_open(void) { return false; }
static unsigned test_layer = 1;
static uint32_t test_extra_layers;
static atomic_t test_battery_reads;
static inline uint8_t zmk_keymap_highest_layer_active(void) { return test_layer; }
static inline uint8_t zmk_keymap_layer_index_to_id(uint8_t index) { return index; }
static inline bool zmk_keymap_layer_active(uint8_t id) {
    return id == 0 || id == test_layer || (test_extra_layers & BIT(id));
}
static inline uint8_t zmk_battery_state_of_charge(void) { atomic_inc(&test_battery_reads); return 0; }
typedef struct { unsigned type; uint8_t value; } zmk_event_t;
struct zmk_battery_state_changed { uint8_t state_of_charge; };
struct zmk_activity_state_changed { unsigned state; };
enum { ZMK_ACTIVITY_ACTIVE, ZMK_ACTIVITY_IDLE, ZMK_ACTIVITY_SLEEP };
static inline struct zmk_battery_state_changed *as_zmk_battery_state_changed(const zmk_event_t *e) {
    static struct zmk_battery_state_changed value; value.state_of_charge = e->value; return &value;
}
static inline struct zmk_activity_state_changed *as_zmk_activity_state_changed(const zmk_event_t *e) {
    static struct zmk_activity_state_changed value; value.state = e->value; return e->type == 1 ? &value : NULL;
}
#define ZMK_LISTENER(name, cb) static int (*const name)(const zmk_event_t *) __unused = cb
#define ZMK_SUBSCRIPTION(...)
struct zmk_behavior_binding { const char *behavior_dev; };
struct zmk_behavior_binding_event { unsigned unused; };
struct behavior_driver_api {
    int (*binding_pressed)(struct zmk_behavior_binding *, struct zmk_behavior_binding_event);
    int (*binding_released)(struct zmk_behavior_binding *, struct zmk_behavior_binding_event);
    unsigned locality;
};
#define BEHAVIOR_LOCALITY_GLOBAL 1
#define ZMK_BEHAVIOR_OPAQUE 0
static struct device test_binding;
static inline const struct device *zmk_behavior_get_binding(const char *name) { ARG_UNUSED(name); return &test_binding; }
