/* SPDX-License-Identifier: MIT */
#include "../../src/widget.c"
#include <nsi_main.h>
#include <zephyr/drivers/gpio/gpio_emul.h>
#include <zephyr/sys/__assert.h>
int test_battery_press(void);
static gpio_port_value_t unrelated_output;

BUILD_ASSERT(!DT_SAME_NODE(DT_COMPAT_GET_ANY_STATUS_OKAY(gpio_leds),
                           DT_PARENT(DT_ALIAS(led_red))),
             "The fixture must exercise an unrelated first gpio-leds device");

static uint8_t output(void) {
  gpio_port_value_t value;
  __ASSERT_NO_MSG(gpio_emul_output_get_masked(
                      DEVICE_DT_GET(DT_NODELABEL(gpio0)), 0x39, &value) == 0);
  __ASSERT((value & 1) == unrelated_output, "Unrelated LED output changed");
  return (value >> 3) & 7;
}
static void activity(unsigned state) {
  zmk_event_t event = {1, state};
  led_activity_listener_cb(&event);
  printk("activity=%u output=%u time=%lld\n", state, output(), k_uptime_get());
}
static void wait_color(uint8_t color);

#if !SHOW_LAYER_SINGLE_PULSE
static K_SEM_DEFINE(connectivity_blocked, 0, 1);
static K_SEM_DEFINE(connectivity_release, 0, 1);
static void block_connectivity_work(struct k_work *work) {
  ARG_UNUSED(work);
  k_sem_give(&connectivity_blocked);
  __ASSERT_NO_MSG(k_sem_take(&connectivity_release, K_SECONDS(2)) == 0);
}
static K_WORK_DEFINE(connectivity_blocker, block_connectivity_work);

static void test_connectivity_sleep(void) {
  k_work_submit(&connectivity_blocker);
  __ASSERT_NO_MSG(k_sem_take(&connectivity_blocked, K_SECONDS(2)) == 0);
  indicate_connectivity();
  activity(ZMK_ACTIVITY_SLEEP);
  indicate_connectivity();
  activity(ZMK_ACTIVITY_ACTIVE);
  uint8_t restored = output();
  k_sem_give(&connectivity_release);
  int64_t until = k_uptime_get() + 60;
  while (k_uptime_get() < until) {
    __ASSERT(output() == restored,
             "Pre-sleep connectivity displayed after wake");
    k_msleep(1);
  }
  __ASSERT_NO_MSG(k_msgq_num_used_get(&led_msgq) == 0);
  indicate_connectivity();
  wait_color(4);
  k_msleep(CONFIG_RGBLED_WIDGET_CONN_BLINK_MS +
           CONFIG_RGBLED_WIDGET_INTERVAL_MS + 20);
  __ASSERT_NO_MSG(output() == restored);
  printk("RGB_CONNECTIVITY_SLEEP_COMPLETE pending asleep fresh-wake PASS\n");
}

static void test_endpoint_colors(void) {
#if !IS_ENABLED(CONFIG_ZMK_SPLIT) || IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
  test_transport = ZMK_TRANSPORT_USB;
  const uint8_t ble_colors[] = {1, 3, 4};
  for (unsigned state = 0; state < ARRAY_SIZE(ble_colors); state++) {
    test_profile_open = state == 1;
    test_profile_connected = state == 2;
    indicate_connectivity_internal(current_generation());
    uint8_t expected = IS_ENABLED(CONFIG_RGBLED_WIDGET_CONN_SHOW_USB)
                           ? CONFIG_RGBLED_WIDGET_CONN_COLOR_USB
                           : ble_colors[state];
    wait_color(expected);
    k_msleep(CONFIG_RGBLED_WIDGET_CONN_BLINK_MS +
             2 * CONFIG_RGBLED_WIDGET_INTERVAL_MS + 20);
    __ASSERT_NO_MSG(output() == led_layer_color);
  }
  test_transport = ZMK_TRANSPORT_BLE;
  test_profile_open = false;
  test_profile_connected = true;
#else
  for (unsigned connected = 0; connected < 2; connected++) {
    test_peripheral_connected = connected;
    indicate_connectivity_internal(current_generation());
    wait_color(connected ? CONFIG_RGBLED_WIDGET_CONN_COLOR_CONNECTED
                         : CONFIG_RGBLED_WIDGET_CONN_COLOR_DISCONNECTED);
    k_msleep(CONFIG_RGBLED_WIDGET_CONN_BLINK_MS +
             CONFIG_RGBLED_WIDGET_INTERVAL_MS + 20);
    __ASSERT_NO_MSG(output() == led_layer_color);
  }
  test_peripheral_connected = true;
#endif
  printk("RGB_ENDPOINT_COLORS_COMPLETE selected-output profile-state PASS\n");
}
#endif
static void wait_color(uint8_t color) {
  int64_t limit = k_uptime_get() + 300;
  while (output() != color && k_uptime_get() < limit) {
    k_msleep(1);
  }
  __ASSERT_NO_MSG(output() == color);
}

#if SHOW_LAYER_SINGLE_PULSE
static void require_quiet(uint32_t duration_ms) {
  int64_t deadline = k_uptime_get() + duration_ms;
  while (k_uptime_get() < deadline) {
    __ASSERT(output() == 0, "A retired debounced pulse lit after wake");
    k_msleep(1);
  }
  __ASSERT_NO_MSG(output() == 0 && k_msgq_num_used_get(&led_msgq) == 0);
}

static void test_debounced_sleep(void) {
  zmk_event_t change = {0, 1};
  test_layer = 0;
  k_mutex_lock(&led_state_lock, K_FOREVER);
  last_indicated_layer = 0;
  k_mutex_unlock(&led_state_lock);

  test_layer = 3;
  led_layer_listener_cb(&change);
  __ASSERT_NO_MSG(k_work_delayable_is_pending(&layer_indicate_work));
  activity(ZMK_ACTIVITY_SLEEP);
  activity(ZMK_ACTIVITY_ACTIVE);
  require_quiet(CONFIG_RGBLED_WIDGET_LAYER_DEBOUNCE_MS + 30);

  activity(ZMK_ACTIVITY_SLEEP);
  test_layer = 4;
  led_layer_listener_cb(&change);
  __ASSERT(!k_work_delayable_is_pending(&layer_indicate_work),
           "A layer change scheduled a new pulse while sleeping");
  activity(ZMK_ACTIVITY_ACTIVE);
  require_quiet(CONFIG_RGBLED_WIDGET_LAYER_DEBOUNCE_MS + 30);

  /* A new post-wake request still works when old work was pending. */
  test_layer = 5;
  led_layer_listener_cb(&change);
  activity(ZMK_ACTIVITY_SLEEP);
  activity(ZMK_ACTIVITY_ACTIVE);
  test_layer = 6;
  led_layer_listener_cb(&change);
  wait_color(6);
  k_msleep(CONFIG_RGBLED_WIDGET_LAYER_BLINK_MS + 20);
  __ASSERT_NO_MSG(output() == 0 && k_msgq_num_used_get(&led_msgq) == 0);
  printk("RGB_DEBOUNCED_SLEEP_COMPLETE pending asleep post-wake PASS\n");
}
#endif

int main(void) {
  k_thread_suspend(led_init_tid);
  __ASSERT(led_dev == DEVICE_DT_GET(DT_PARENT(DT_ALIAS(led_red))),
           "RGB aliases must select their own GPIO LED device");
  __ASSERT_NO_MSG(
      gpio_emul_output_get_masked(DEVICE_DT_GET(DT_NODELABEL(gpio0)), 1,
                                  &unrelated_output) == 0);
#if SHOW_LAYER_SINGLE_PULSE
  led_init_thread(NULL, NULL, NULL);
  __ASSERT_NO_MSG(initialized && output() == 0 &&
                  k_msgq_num_used_get(&led_msgq) == 0);
  indicate_connectivity();
  indicate_connectivity_internal(current_generation());
  __ASSERT_NO_MSG(test_battery_press() == 0);
  zmk_event_t critical = {0, 1};
  led_battery_listener_cb(&critical);
  k_msleep(120);
  __ASSERT_NO_MSG(output() == 0 && atomic_get(&test_battery_reads) == 0);
  __ASSERT_NO_MSG(k_msgq_num_used_get(&led_msgq) == 0);
  test_debounced_sleep();
#if CONFIG_RGBLED_WIDGET_LAYER_IGNORE_MASK
  zmk_event_t change = {0, 1};
  test_layer = 0;
  last_indicated_layer = 0;
  for (unsigned i = 0; i < 30; ++i) {
    test_layer = 9;
    led_layer_listener_cb(&change);
    test_layer = 0;
    led_layer_listener_cb(&change);
  }
  k_msleep(30);
  __ASSERT_NO_MSG(output() == 0 && k_msgq_num_used_get(&led_msgq) == 0);
  test_layer = 9;
  test_extra_layers = BIT(6);
  led_layer_listener_cb(&change);
  wait_color(6);
  k_msleep(CONFIG_RGBLED_WIDGET_LAYER_BLINK_MS + 20);
  __ASSERT_NO_MSG(output() == 0);
  test_layer = 6;
  test_extra_layers = 0;
  led_layer_listener_cb(&change);
  k_msleep(30);
  __ASSERT_NO_MSG(output() == 0 && k_msgq_num_used_get(&led_msgq) == 0);
  /* A newer layer replaces a running one-second indication immediately. */
  test_layer = 2;
  indicate_layer();
  wait_color(2);
  test_layer = 5;
  indicate_layer();
  wait_color(5);
  k_msleep(CONFIG_RGBLED_WIDGET_LAYER_BLINK_MS - 20);
  __ASSERT_NO_MSG(output() == 5);
  k_msleep(40);
  __ASSERT_NO_MSG(output() == 0);
#endif
  test_layer = 3;
  indicate_layer();
  wait_color(3);
  k_msleep(CONFIG_RGBLED_WIDGET_LAYER_BLINK_MS + 20);
  __ASSERT_NO_MSG(output() == 0);
  test_layer = 4;
  indicate_layer();
  wait_color(4);
  activity(ZMK_ACTIVITY_SLEEP);
  test_layer = 2;
  indicate_layer();
  activity(ZMK_ACTIVITY_ACTIVE);
  k_msleep(30);
  __ASSERT_NO_MSG(output() == 0 && k_msgq_num_used_get(&led_msgq) == 0);
  test_layer = 1;
  indicate_layer();
  wait_color(1);
  k_msleep(CONFIG_RGBLED_WIDGET_LAYER_BLINK_MS + 20);
  __ASSERT_NO_MSG(output() == 0);
  printk(
      "RGB_NATIVE_COMPLETE quiet-status one-layer-pulse off sleep-wake PASS\n");
  nsi_exit(0);
  return 0;
#elif defined(RGB_WIDGET_STATUS_ONLY_TEST)
  indicate_connectivity();
  k_msleep(20);
  __ASSERT_NO_MSG(k_msgq_num_used_get(&led_msgq) == 1);
  initialized = true;
  activity(ZMK_ACTIVITY_SLEEP);
  k_msleep(130);
  __ASSERT_NO_MSG(output() == 0);
  activity(ZMK_ACTIVITY_ACTIVE);
  __ASSERT_NO_MSG(output() == 0);

  indicate_connectivity_internal(current_generation());
  wait_color(4);
  activity(ZMK_ACTIVITY_SLEEP);
  activity(ZMK_ACTIVITY_ACTIVE);
  k_msleep(160);
  __ASSERT_NO_MSG(output() == 0 && led_current_color == 0);

  int64_t start = k_uptime_get();
  __ASSERT_NO_MSG(test_battery_press() == 0);
  __ASSERT_NO_MSG(k_uptime_get() == start &&
                  atomic_get(&test_battery_reads) == 0);
  k_msleep(10);
  __ASSERT_NO_MSG(atomic_get(&test_battery_reads) > 0);
  activity(ZMK_ACTIVITY_SLEEP);
  activity(ZMK_ACTIVITY_IDLE);
  k_msleep(1200);
  __ASSERT_NO_MSG(output() == 0 && led_current_color == 0);
  test_connectivity_sleep();
  test_endpoint_colors();
  printk(
      "RGB_NATIVE_COMPLETE status-only queued running retry sleep-wake PASS\n");
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

  indicate_connectivity_internal(current_generation());
  wait_color(4);
  activity(ZMK_ACTIVITY_SLEEP);
  test_layer = 3;
  update_layer_color();
  activity(ZMK_ACTIVITY_ACTIVE);
  k_msleep(160);
  __ASSERT_NO_MSG(output() == 3 && led_current_color == 3);

  int64_t start = k_uptime_get();
  __ASSERT_NO_MSG(test_battery_press() == 0);
  __ASSERT_NO_MSG(k_uptime_get() == start &&
                  atomic_get(&test_battery_reads) == 0);
  k_msleep(10);
  __ASSERT_NO_MSG(atomic_get(&test_battery_reads) > 0);
  activity(ZMK_ACTIVITY_SLEEP);
  test_layer = 2;
  update_layer_color();
  activity(ZMK_ACTIVITY_IDLE);
  k_msleep(1200);
  __ASSERT_NO_MSG(output() == 2 && led_current_color == 2);
  test_connectivity_sleep();
  test_endpoint_colors();
  printk("RGB_NATIVE_COMPLETE queued-sleep running-sleep-wake caller "
         "retry-sleep-wake PASS\n");
  nsi_exit(0);
  return 0;
#endif
}
