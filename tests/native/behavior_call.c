/* SPDX-License-Identifier: MIT */
#include "../../src/behaviors/behavior_rgbled_widget.c"

int test_battery_press(void) {
    static const struct behavior_rgb_wdg_config cfg = {.indicate_battery = true};
    test_binding.config = &cfg;
    struct zmk_behavior_binding binding = {"test"};
    behavior_rgb_wdg_init(&test_binding);
    return on_keymap_binding_pressed(&binding, (struct zmk_behavior_binding_event){0});
}
