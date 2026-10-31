/* SPDX-License-Identifier: MIT */
#include <zephyr/drivers/gpio/gpio_emul.h>
#include <zephyr/sys/__assert.h>
#include <nsi_main.h>
#include "../../src/widget.c"
int test_battery_press(void);
static gpio_port_value_t unrelated_output;

BUILD_ASSERT(!DT_SAME_NODE(DT_COMPAT_GET_ANY_STATUS_OKAY(gpio_leds),
                          DT_PARENT(DT_ALIAS(led_red))),
             "The fixture must exercise an unrelated first gpio-leds device");

static uint8_t output(void) {
    gpio_port_value_t value;
    __ASSERT_NO_MSG(gpio_emul_output_get_masked(DEVICE_DT_GET(DT_NODELABEL(gpio0)), 0x39, &value) == 0);
    __ASSERT((value & 1) == unrelated_output, "Unrelated LED output changed");
    return (value >> 3) & 7;
}
static void activity(unsigned state) {
    zmk_event_t event = {1, state};
#if SHOW_LAYER_SINGLE_PULSE
    led_layer_pulse_activity_cb(&event);
#else
    led_layer_color_listener_cb(&event);
#endif
    printk("activity=%u output=%u time=%lld\n", state, output(), k_uptime_get());
}
static void wait_color(uint8_t color) {
    int64_t limit = k_uptime_get() + 300;
    while (output() != color && k_uptime_get() < limit) { k_msleep(1); }
    __ASSERT_NO_MSG(output() == color);
}
int main(void) {
    k_thread_suspend(led_init_tid);
    __ASSERT(led_dev == DEVICE_DT_GET(DT_PARENT(DT_ALIAS(led_red))),
             "RGB aliases must select their own GPIO LED device");
    __ASSERT_NO_MSG(gpio_emul_output_get_masked(DEVICE_DT_GET(DT_NODELABEL(gpio0)),
                                             1, &unrelated_output) == 0);
#if SHOW_LAYER_SINGLE_PULSE
    led_init_thread(NULL, NULL, NULL);
    __ASSERT_NO_MSG(initialized && output() == 0 && k_msgq_num_used_get(&led_msgq) == 0);
    indicate_connectivity();
    indicate_connectivity_internal();
    __ASSERT_NO_MSG(test_battery_press() == 0);
    zmk_event_t critical = {0, 1};
    led_battery_listener_cb(&critical);
    k_msleep(120);
    __ASSERT_NO_MSG(output() == 0 && atomic_get(&test_battery_reads) == 0);
    __ASSERT_NO_MSG(k_msgq_num_used_get(&led_msgq) == 0);
#if CONFIG_RGBLED_WIDGET_LAYER_IGNORE_MASK
    zmk_event_t change = {0, 1};
    test_layer = 0;
    last_indicated_layer = 0;
    for (unsigned i = 0; i < 30; ++i) {
        test_layer = 9; led_layer_listener_cb(&change);
        test_layer = 0; led_layer_listener_cb(&change);
    }
    k_msleep(30);
    __ASSERT_NO_MSG(output() == 0 && k_msgq_num_used_get(&led_msgq) == 0);
    test_layer = 9; test_extra_layers = BIT(6); led_layer_listener_cb(&change);
    wait_color(6); k_msleep(CONFIG_RGBLED_WIDGET_LAYER_BLINK_MS + 20);
    __ASSERT_NO_MSG(output() == 0);
    test_layer = 6; test_extra_layers = 0; led_layer_listener_cb(&change);
    k_msleep(30);
    __ASSERT_NO_MSG(output() == 0 && k_msgq_num_used_get(&led_msgq) == 0);
    /* A newer layer replaces a running one-second indication immediately. */
    test_layer = 2; indicate_layer(); wait_color(2);
    test_layer = 5; indicate_layer(); wait_color(5);
    k_msleep(CONFIG_RGBLED_WIDGET_LAYER_BLINK_MS - 20);
    __ASSERT_NO_MSG(output() == 5);
    k_msleep(40);
    __ASSERT_NO_MSG(output() == 0);
#endif
    test_layer = 3; indicate_layer();
    wait_color(3);
    k_msleep(CONFIG_RGBLED_WIDGET_LAYER_BLINK_MS + 20);
    __ASSERT_NO_MSG(output() == 0);
    test_layer = 4; indicate_layer();
    wait_color(4);
    activity(ZMK_ACTIVITY_SLEEP);
    test_layer = 2; indicate_layer();
    activity(ZMK_ACTIVITY_ACTIVE);
    k_msleep(30);
    __ASSERT_NO_MSG(output() == 0 && k_msgq_num_used_get(&led_msgq) == 0);
    test_layer = 1; indicate_layer();
    wait_color(1);
    k_msleep(CONFIG_RGBLED_WIDGET_LAYER_BLINK_MS + 20);
    __ASSERT_NO_MSG(output() == 0);
    printk("RGB_NATIVE_COMPLETE quiet-status one-layer-pulse off sleep-wake PASS\n");
    nsi_exit(0);
    return 0;
#else
    /* Public behavior calls are valid before the delayed LED thread starts. */
    indicate_connectivity();
    k_msleep(20);
    __ASSERT_NO_MSG(k_msgq_num_used_get(&led_msgq) == 1);
    initialized = true;
    update_layer_color();
    activity(ZMK_ACTIVITY_SLEEP);
    k_msleep(130);
    __ASSERT_NO_MSG(output() == 0);
    activity(ZMK_ACTIVITY_ACTIVE);
    __ASSERT_NO_MSG(output() == 1);

    indicate_connectivity_internal();
    wait_color(4);
    activity(ZMK_ACTIVITY_SLEEP);
    test_layer = 3; update_layer_color();
    activity(ZMK_ACTIVITY_ACTIVE);
    k_msleep(160);
    __ASSERT_NO_MSG(output() == 3 && led_current_color == 3);

    int64_t start = k_uptime_get();
    __ASSERT_NO_MSG(test_battery_press() == 0);
    __ASSERT_NO_MSG(k_uptime_get() == start && atomic_get(&test_battery_reads) == 0);
    k_msleep(10);
    __ASSERT_NO_MSG(atomic_get(&test_battery_reads) > 0);
    activity(ZMK_ACTIVITY_SLEEP);
    test_layer = 2; update_layer_color();
    activity(ZMK_ACTIVITY_IDLE);
    k_msleep(1200);
    __ASSERT_NO_MSG(output() == 2 && led_current_color == 2);
    printk("RGB_NATIVE_COMPLETE queued-sleep running-sleep-wake caller retry-sleep-wake PASS\n");
    nsi_exit(0);
    return 0;
#endif
}
