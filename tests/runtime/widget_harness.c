/* SPDX-License-Identifier: MIT */
#include "stubs.h"
#include "../../src/widget.c"
#include "../../src/behaviors/behavior_rgbled_widget.c"

static void reset(void) {
    led_msgq.count = led_msgq.head = 0; now_ms = gpio_bits = edges = sleeps = 0;
    initialized = false; led_current_color = led_layer_color = 0;
    led_sleeping = false; led_generation = 0; sleep_hook = NULL;
    assert(!mutex_depth && !led_state_lock.depth);
    indicate_connectivity_work.due = indicate_connectivity_work.schedules = 0;
#if SHOW_LAYER_CHANGE
    layer_indicate_work.due = layer_indicate_work.schedules = 0;
#endif
#if SHOW_LAYER_SINGLE_PULSE
    last_indicated_layer = 0;
    layer_pulse_serial = 0;
    k_sem_reset(&layer_pulse_changed);
#endif
    selected = preferred = ZMK_TRANSPORT_NONE; profile_open = peripheral_connected = false;
    layer = 0; battery = 80; battery_reads = battery_ready_after = 0;
    extra_active_layers = 0; reorder_layers = false;
    memset(peripheral_reads, 0, sizeof(peripheral_reads));
    memset(peripheral_ready_after, 0, sizeof(peripheral_ready_after));
    peripheral_battery[0] = 20; peripheral_battery[1] = 19;
    peripheral_error[0] = peripheral_error[1] = 0;
}
static struct blink_item item(unsigned i) {
    assert(i < led_msgq.count); struct blink_item v;
    memcpy(&v, led_msgq.data[(led_msgq.head + i) % 16], sizeof(v)); return v;
}
static void run_worker(void) { if (!setjmp(worker_done)) led_process_thread(NULL, NULL, NULL); }
static void test_gpio_and_worker(void) {
    reset(); set_rgb_leds(5, 30, 0);
    assert(edges == 2 && gpio_bits == 5 && now_ms == 30);
    assert(gpio_trace[0].channel == 0 && gpio_trace[1].channel == 2);
    assert(gpio_trace[0].at == 0 && gpio_trace[1].at == 0);
    set_rgb_leds(5, 0, 0); assert(edges == 2 && sleeps == 1);
    set_rgb_leds(2, 20, 0); assert(edges == 5 && gpio_bits == 2 && now_ms == 50);
    assert(gpio_trace[2].channel == 0 && !gpio_trace[2].on);
    assert(gpio_trace[3].channel == 1 && gpio_trace[3].on);
    assert(gpio_trace[4].channel == 2 && !gpio_trace[4].on);
    reset(); led_layer_color = 2; set_rgb_leds(2, 0, 0);
    struct blink_item same = {.color = 2, .duration_ms = 100, .sleep_ms = 70};
    struct blink_item next = {.color = 4, .duration_ms = 50};
    assert(!k_msgq_put(&led_msgq, &same, 0)); assert(!k_msgq_put(&led_msgq, &next, 0));
    run_worker();
    const unsigned expected[] = {500, 100, 500, 70, 50, 500};
    assert(sleeps == 6 && !memcmp(sleep_trace, expected, sizeof(expected)));
    assert(now_ms == 1720 && gpio_bits == 2 && led_current_color == 2 && !led_msgq.count);
    const unsigned times[] = {0, 0, 500, 600, 1100, 1170, 1170, 1220, 1220};
    const unsigned channels[] = {1, 1, 1, 1, 1, 1, 2, 1, 2};
    const unsigned states[] = {1, 0, 1, 0, 1, 0, 1, 1, 0};
    assert(edges == 9);
    for (unsigned i = 0; i < 9; i++) {
        assert(gpio_trace[i].at == times[i] && gpio_trace[i].channel == channels[i] && gpio_trace[i].on == states[i]);
    }
    reset(); led_layer_color = 7;
    struct blink_item immediate = {.color = 7}; k_msgq_put(&led_msgq, &immediate, 0);
    run_worker(); assert(gpio_bits == 7 && edges == 3 && sleeps == 0);
    /* Zero-duration connectivity/battery colors are valid non-layer items. */
    const unsigned status_colors[] = {4, 1};
    for (unsigned i = 0; i < 2; i++) {
        reset(); led_layer_color = 2;
        struct blink_item status = {.color = status_colors[i]};
        assert(!k_msgq_put(&led_msgq, &status, K_NO_WAIT));
        run_worker();
        assert(gpio_bits == status_colors[i] && led_current_color == status_colors[i]);
        assert(led_layer_color == 2 && sleeps == 0);
    }
}
static void test_connectivity_and_queue(void) {
    reset(); zmk_event_t ev = {0}; led_output_listener_cb(&ev);
    assert(indicate_connectivity_work.schedules == 0);
    initialized = true; led_output_listener_cb(&ev); now_ms = 7; led_output_listener_cb(&ev);
    assert(indicate_connectivity_work.schedules == 2 && indicate_connectivity_work.due == 23);
    assert(led_msgq.count == 0); /* rescheduling does not itself emit */
#if !CONFIG_ZMK_SPLIT || CONFIG_ZMK_SPLIT_ROLE_CENTRAL
    const int routes[] = {ZMK_TRANSPORT_NONE, ZMK_TRANSPORT_USB, ZMK_TRANSPORT_BLE};
    for (unsigned r = 0; r < 3; r++) for (unsigned p = 0; p < 3; p++) for (unsigned open = 0; open < 2; open++) {
        led_msgq.count = led_msgq.head = 0; selected = routes[r]; preferred = routes[p]; profile_open = open;
        indicate_connectivity_cb(NULL);
        unsigned expected = r == 1 && CONFIG_RGBLED_WIDGET_CONN_SHOW_USB ? 6 :
                            r > 0 && CONFIG_ZMK_BLE ? 4 :
                            CONFIG_ZMK_BLE && p > 0 && open ? 3 : 1;
        assert(led_msgq.count == 1 && item(0).color == expected && item(0).duration_ms == 1000);
    }
#else
    for (unsigned connected = 0; connected < 2; connected++) {
        led_msgq.count = led_msgq.head = 0; peripheral_connected = connected;
        indicate_connectivity_cb(NULL); assert(item(0).color == (connected ? 4 : 1));
    }
#endif
    reset(); selected = ZMK_TRANSPORT_BLE; peripheral_connected = true;
    for (unsigned i = 0; i < 16; i++) indicate_connectivity_internal();
    selected = ZMK_TRANSPORT_NONE; peripheral_connected = false;
    indicate_connectivity_internal();
    assert(led_msgq.count == 16); /* current bounded queue drops the new item */
    for (unsigned i = 0; i < 16; i++) assert(item(i).duration_ms == 1000 && item(i).color == (CONFIG_ZMK_BLE ? 4 : 1));
}
static void test_battery(void) {
    reset(); const unsigned levels[] = {0, 1, 5, 6, 19, 20, 79, 80, 100};
    const unsigned colors[] = {5, 1, 1, 1, 1, 3, 3, 2, 2};
    for (unsigned i = 0; i < 9; i++) {
        assert(get_battery_color(levels[i]) == colors[i]);
        zmk_event_t ev = {0, levels[i]}; led_msgq.count = led_msgq.head = 0;
        initialized = false; led_battery_listener_cb(&ev); assert(!led_msgq.count);
        initialized = true; led_battery_listener_cb(&ev);
        assert(led_msgq.count == (levels[i] > 0 && levels[i] <= 5));
        if (led_msgq.count) assert(item(0).color == 1 && item(0).duration_ms == 2000);
    }
    reset(); battery = 0; indicate_battery_internal(led_generation);
#if CONFIG_RGBLED_WIDGET_BATTERY_SHOW_SELF || CONFIG_RGBLED_WIDGET_BATTERY_SHOW_PERIPHERALS
    assert(now_ms == 1000 && battery_reads == 11 && item(0).color == 5);
    reset(); battery = 80; battery_ready_after = 3; indicate_battery_internal(led_generation);
    assert(now_ms == 300 && battery_reads == 4 && item(0).color == 2);
#else
    assert(battery_reads == 0);
#endif
#if CONFIG_RGBLED_WIDGET_BATTERY_SHOW_PERIPHERALS || CONFIG_RGBLED_WIDGET_BATTERY_SHOW_ONLY_PERIPHERALS
    unsigned offset = CONFIG_RGBLED_WIDGET_BATTERY_SHOW_PERIPHERALS ? 1 : 0;
    assert(led_msgq.count == offset + 2 && item(offset).color == 3 && item(offset + 1).color == 1);
    reset(); peripheral_error[0] = -EIO; peripheral_battery[1] = 0; indicate_battery_internal(led_generation);
    assert(led_msgq.count == offset + 1 && item(offset).color == 5);
    assert(peripheral_reads[0] == 1 && peripheral_reads[1] == 26 && now_ms == 2500);
#endif
}
static void test_layers_and_boot(void) {
    reset();
#if !CONFIG_ZMK_SPLIT || CONFIG_ZMK_SPLIT_ROLE_CENTRAL
    indicate_layer(); assert(!led_msgq.count);
    layer = 3; indicate_layer(); assert(led_msgq.count == 3);
    assert(item(0).sleep_ms == 100 && item(1).sleep_ms == 100 && item(2).sleep_ms == 0);
    for (unsigned i = 0; i < 3; i++) assert(item(i).color == 6 && item(i).duration_ms == 100);
#endif
#if SHOW_LAYER_CHANGE
    reset(); zmk_event_t e = {0, 1}; led_layer_listener_cb(&e); assert(layer_indicate_work.schedules == 0);
    initialized = true; e.value = 0; led_layer_listener_cb(&e); assert(layer_indicate_work.schedules == 0);
    e.value = 1; led_layer_listener_cb(&e); assert(layer_indicate_work.schedules == 1 && layer_indicate_work.due == 100);
    layer = 2; indicate_layer_cb(NULL); assert(led_msgq.count == 2);
#endif
#if SHOW_LAYER_COLORS
    reset();
    for (unsigned i = 0; i < 32; i++) {
        led_msgq.count = led_msgq.head = 0; layer = i; unsigned before = led_layer_color;
        unsigned expected = i < 8 ? i : 0; update_layer_color();
        assert(led_layer_color == expected && led_msgq.count == (before != expected));
        if (led_msgq.count) assert(item(0).color == expected && item(0).duration_ms == 0 && item(0).persistent_color);
        update_layer_color(); assert(led_msgq.count == (before != expected));
    }
    /* A dropped color invalidation must converge to the latest target after
     * the bounded queue drains, rather than restoring an older queued color. */
    const unsigned targets[] = {7, 0, 3};
    for (unsigned t = 0; t < 3; t++) {
        reset();
        set_rgb_leds(7, 0, 0);
        for (unsigned i = 0; i < 16; i++) {
            layer = 1 + i % 2;
            update_layer_color();
        }
        assert(led_msgq.count == 16);
        layer = 7; update_layer_color();
        layer = 0; update_layer_color();
        layer = targets[t]; update_layer_color();
        assert(led_layer_color == targets[t] && led_msgq.count == 16);
        run_worker();
        assert(gpio_bits == targets[t] && led_current_color == targets[t]);
        assert(led_layer_color == targets[t] && !led_msgq.count && sleeps == 0);
        update_layer_color();
        assert(!led_msgq.count);
        layer = 1; update_layer_color();
        assert(led_msgq.count == 1);
        run_worker();
        assert(gpio_bits == 1 && led_current_color == 1 && !led_msgq.count);
    }

    reset(); layer = 1; zmk_event_t change = {0, 1}; led_layer_color_listener_cb(&change); assert(!led_msgq.count);
    initialized = true; led_layer_color_listener_cb(&change); assert(item(0).color == 1);
    set_rgb_leds(1, 0, 0); zmk_event_t sleep = {1, ZMK_ACTIVITY_SLEEP}; led_layer_color_listener_cb(&sleep);
    assert(!gpio_bits && led_layer_color == 1); /* persistent target survives sleep */
#endif
    reset(); led_init_thread(NULL, NULL, NULL);
    assert(initialized && indicate_connectivity_work.schedules == 1);
    assert(indicate_connectivity_work.due == now_ms + 16 && now_ms == 2500);
}
static void sleep_event(void) {
#if SHOW_LAYER_COLORS
    zmk_event_t event = {1, ZMK_ACTIVITY_SLEEP};
    led_layer_color_listener_cb(&event);
    assert(!gpio_bits && !led_current_color);
#endif
}
static void sleep_and_wake_event(void) {
#if SHOW_LAYER_COLORS
    sleep_event();
    layer = 3; update_layer_color();
    zmk_event_t event = {1, ZMK_ACTIVITY_ACTIVE};
    led_layer_color_listener_cb(&event);
    assert(gpio_bits == 3 && led_current_color == 3);
#endif
}
static void test_caller_and_sleep(void) {
    reset(); battery = peripheral_battery[0] = peripheral_battery[1] = 0;
    indicate_battery();
    assert(!now_ms && !sleeps && !battery_reads && !peripheral_reads[0]);
    assert(led_msgq.count == 1 && item(0).battery_request);
    run_worker();
    assert(!led_msgq.count && battery_reads ==
           (CONFIG_RGBLED_WIDGET_BATTERY_SHOW_SELF || CONFIG_RGBLED_WIDGET_BATTERY_SHOW_PERIPHERALS ? 11 : 0));
    for (unsigned i = 0; i < 17; ++i) indicate_battery();
    assert(led_msgq.count == 16); /* The request path is bounded and never retries the queue. */
#if SHOW_LAYER_COLORS
    reset(); initialized = true; layer = 1; update_layer_color();
    sleep_event(); run_worker();
    assert(!gpio_bits && !led_current_color && led_layer_color == 1);
    indicate_battery(); indicate_connectivity_internal(); layer = 2; update_layer_color();
    assert(!led_msgq.count && led_layer_color == 2 && !gpio_bits);
    zmk_event_t wake = {1, ZMK_ACTIVITY_IDLE}; led_layer_color_listener_cb(&wake);
    assert(gpio_bits == 2 && led_current_color == 2);
    for (unsigned i = 0; i < 3; ++i) {
        reset(); initialized = true; layer = 1; update_layer_color(); run_worker();
        indicate_connectivity_internal();
        sleep_hook = i == 0 ? sleep_event : sleep_and_wake_event;
        if (i == 2) { /* Pause inside battery retries instead of inside a blink. */
            led_msgq.count = led_msgq.head = 0;
            battery = 0; indicate_battery();
        }
        run_worker();
        assert(gpio_bits == (i == 0 ? 0 : 3));
        assert(led_current_color == gpio_bits && !led_msgq.count);
    }
    /* Full old queues cannot prevent synchronous wake restoration. */
    reset(); initialized = true;
    for (unsigned i = 0; i < 16; ++i) indicate_connectivity_internal();
    sleep_and_wake_event(); run_worker();
    assert(gpio_bits == 3 && led_current_color == 3 && !led_msgq.count);
#endif
}
static void test_behavior(void) {
    reset(); struct behavior_rgb_wdg_config cfg = {false, false, false}; binding_device.config = &cfg;
    struct zmk_behavior_binding binding = {"rgb"}; struct zmk_behavior_binding_event event = {0};
    assert(behavior_rgb_wdg_init(&binding_device) == 0);
    assert(on_keymap_binding_pressed(&binding, event) == ZMK_BEHAVIOR_OPAQUE && !led_msgq.count);
    cfg.indicate_connectivity = true; on_keymap_binding_pressed(&binding, event);
    assert(indicate_connectivity_work.schedules == 1);
    cfg.indicate_connectivity = false; cfg.indicate_battery = true; on_keymap_binding_pressed(&binding, event);
    unsigned count = led_msgq.count; assert(count > 0);
    cfg.indicate_battery = false; cfg.indicate_layer = true; layer = 2; on_keymap_binding_pressed(&binding, event);
#if !CONFIG_ZMK_SPLIT || CONFIG_ZMK_SPLIT_ROLE_CENTRAL
    assert(led_msgq.count == count + 2);
#else
    assert(led_msgq.count == count);
#endif
    count = led_msgq.count; assert(on_keymap_binding_released(&binding, event) == ZMK_BEHAVIOR_OPAQUE);
    assert(led_msgq.count == count && behavior_rgb_wdg_driver_api.locality == BEHAVIOR_LOCALITY_GLOBAL);
}
static void test_quiet_layer_pulse(void) {
#if SHOW_LAYER_SINGLE_PULSE
    const unsigned pulse_ms = CONFIG_RGBLED_WIDGET_LAYER_BLINK_MS;
#endif
    reset();
    led_init_thread(NULL, NULL, NULL);
    assert(initialized && !led_msgq.count && !gpio_bits && !edges && !sleeps);
    assert(!indicate_connectivity_work.schedules && !battery_reads);
    for (unsigned i = 0; i < 20; i++) {
        selected = i % 3;
        indicate_connectivity();
        indicate_connectivity_internal();
        indicate_battery();
        indicate_battery_internal(led_generation);
        zmk_event_t critical = {0, 1};
        led_battery_listener_cb(&critical);
    }
    assert(!led_msgq.count && !edges && !sleeps && !battery_reads);
    assert(!indicate_connectivity_work.schedules);
#if SHOW_LAYER_SINGLE_PULSE
    zmk_event_t change = {0, 1};
    layer = 3; led_layer_listener_cb(&change);
    assert(layer_indicate_work.schedules == 1 && layer_indicate_work.due == 100);
    led_layer_listener_cb(&change);
    assert(layer_indicate_work.schedules == 1);
    /* Releasing the highest layer also changes the displayed layer. */
    layer = 0; change.value = 0; led_layer_listener_cb(&change);
    assert(layer_indicate_work.schedules == 2);
    reset(); initialized = true;
    layer = 3;
    indicate_layer();
    assert(led_msgq.count == 1 && item(0).layer_pulse && item(0).color == 3);
    run_worker();
    assert(!gpio_bits && !led_current_color && !led_layer_color);
    assert(now_ms == pulse_ms && sleep_trace[0] == pulse_ms);
    for (unsigned i = 0; i < edges; i++) assert(gpio_trace[i].at == 0 || gpio_trace[i].at == pulse_ms);
    reset(); initialized = true;
    layer = 2; indicate_layer();
    layer = 4; indicate_layer();
    run_worker();
    assert(!gpio_bits && now_ms == pulse_ms);
    assert(edges == 2 && gpio_trace[0].channel == 2 && gpio_trace[0].on);
    layer = 3; indicate_layer();
    zmk_event_t sleep = {1, ZMK_ACTIVITY_SLEEP};
    led_layer_pulse_activity_cb(&sleep);
    run_worker();
    zmk_event_t wake = {1, ZMK_ACTIVITY_ACTIVE};
    led_layer_pulse_activity_cb(&wake);
    assert(!gpio_bits && !led_msgq.count);
    layer = 0; indicate_layer();
    assert(led_msgq.count == 1 && item(0).layer_pulse && item(0).layer_index == 0);
    run_worker();
    assert(!gpio_bits);
#if CONFIG_RGBLED_WIDGET_LAYER_IGNORE_MASK
    reset(); led_init_thread(NULL, NULL, NULL);
    change.value = 1;
    for (unsigned i = 0; i < 100; ++i) {
        layer = 9; led_layer_listener_cb(&change);
        layer = 0; led_layer_listener_cb(&change);
    }
    assert(!layer_indicate_work.schedules && !led_msgq.count && !edges);
    assert(pulse_layer_index(UINT32_MAX) == UINT8_MAX);
    /* A manual layer below AML still appears. Removing AML alone is silent. */
    layer = 9; extra_active_layers = BIT(6); led_layer_listener_cb(&change);
    assert(layer_indicate_work.schedules == 1);
    indicate_layer_cb(NULL);
    assert(led_msgq.count == 1 && item(0).layer_index == 6 && item(0).color == 6);
    run_worker();
    layer = 6; extra_active_layers = 0; led_layer_listener_cb(&change);
    assert(layer_indicate_work.schedules == 1 && !led_msgq.count);
    layer = 0; led_layer_listener_cb(&change);
    assert(layer_indicate_work.schedules == 2);
    indicate_layer_cb(NULL);
    assert(item(0).layer_index == 0 && item(0).color == 4);
    run_worker();
    /* Queued manual output remains valid when only the ignored layer enters. */
    reset(); initialized = true; layer = 2; indicate_layer();
    layer = 9; extra_active_layers = BIT(2); run_worker();
    assert(edges == 2 && !gpio_bits && now_ms == pulse_ms);
    /* Ignore stable ID9 even after it moves to index3; index9 now holds ID3. */
    reset(); initialized = true; reorder_layers = true;
    layer = 3; led_layer_listener_cb(&change);
    assert(!layer_indicate_work.schedules);
    layer = 9; extra_active_layers = BIT(9); led_layer_listener_cb(&change);
    assert(layer_indicate_work.schedules == 1);
    indicate_layer_cb(NULL);
    assert(item(0).layer_index == 9 && item(0).color == 7);
    run_worker();
#endif
#endif
}

int main(void) {
#if defined(CONFIG_RGBLED_WIDGET_QUIET_PULSE_TEST)
    test_quiet_layer_pulse();
    puts("RGB widget quiet layer pulse: PASS (no boot/status output, one timed color, stale retirement, sleep/wake)");
#else
    test_gpio_and_worker(); test_connectivity_and_queue(); test_battery(); test_layers_and_boot(); test_behavior();
    test_caller_and_sleep();
    puts("RGB widget runtime: PASS groups=6 (controlled APIs, no physical timing claim)");
#endif
    return 0;
}
