/* SPDX-License-Identifier: MIT */
#define main retained_widget_baseline_main
#include "../runtime/widget_harness.c"
#undef main
int main(int argc, char **argv) {
  assert(argc == 2);
  reset();
  if (!strcmp(argv[1], "sleep-queued")) {
    initialized = true;
    layer = 1;
    update_layer_color();
    assert(led_msgq.count == 1);
    zmk_event_t sleep = {1, ZMK_ACTIVITY_SLEEP};
    led_activity_listener_cb(&sleep);
    assert(gpio_bits == 0);
    run_worker();
    printf("sleep-queued: after worker gpio=%u current=%u target=%u\n",
           gpio_bits, led_current_color, led_layer_color);
    fflush(stdout);
    assert(gpio_bits == 0 && "queued RGB work relit output after sleep");
  } else {
    struct behavior_rgb_wdg_config cfg = {true, false, false};
    binding_device.config = &cfg;
    struct zmk_behavior_binding binding = {"rgb"};
    struct zmk_behavior_binding_event event = {0};
    peripheral_battery[0] = peripheral_battery[1] = 80;
    if (!strcmp(argv[1], "local-zero"))
      battery = 0;
    else if (!strcmp(argv[1], "all-zero")) {
      battery = peripheral_battery[0] = peripheral_battery[1] = 0;
    } else
      assert(!strcmp(argv[1], "ready"));
    assert(on_keymap_binding_pressed(&binding, event) == ZMK_BEHAVIOR_OPAQUE);
    printf("%s: caller_wait_ms=%u sleep_calls=%u battery_reads=%u\n", argv[1],
           now_ms, sleeps, battery_reads);
    fflush(stdout);
    assert(now_ms == 0 && "battery key behavior blocks its caller");
  }
  return 0;
}
