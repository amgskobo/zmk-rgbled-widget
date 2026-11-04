#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/led.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>

#include <zmk/battery.h>
#include <zmk/ble.h>
#include <zmk/endpoints.h>
#include <zmk/events/activity_state_changed.h>
#include <zmk/events/battery_state_changed.h>
#include <zmk/events/ble_active_profile_changed.h>
#include <zmk/events/endpoint_changed.h>
#include <zmk/events/layer_state_changed.h>
#include <zmk/events/split_peripheral_status_changed.h>
#include <zmk/keymap.h>
#include <zmk/split/bluetooth/peripheral.h>

#include <zmk/split/central.h>

#include <zephyr/logging/log.h>

#include "rgbled_timeout.h"
#include <zmk_rgbled_widget/widget.h>

#ifndef CONFIG_ZMK_RGBLED_WIDGET_LOG_LEVEL
#define CONFIG_ZMK_RGBLED_WIDGET_LOG_LEVEL 3
#endif
LOG_MODULE_REGISTER(zmk_rgbled_widget_widget,
                    CONFIG_ZMK_RGBLED_WIDGET_LOG_LEVEL);

#define LED_GPIO_NODE_ID DT_PARENT(DT_ALIAS(led_red))

BUILD_ASSERT(DT_NODE_EXISTS(DT_ALIAS(led_red)),
             "An alias for a red LED is not found for RGBLED_WIDGET");
BUILD_ASSERT(DT_NODE_EXISTS(DT_ALIAS(led_green)),
             "An alias for a green LED is not found for RGBLED_WIDGET");
BUILD_ASSERT(DT_NODE_EXISTS(DT_ALIAS(led_blue)),
             "An alias for a blue LED is not found for RGBLED_WIDGET");
BUILD_ASSERT(DT_NODE_HAS_COMPAT(LED_GPIO_NODE_ID, gpio_leds),
             "RGBLED_WIDGET LED aliases must belong to a gpio-leds device");
BUILD_ASSERT(DT_SAME_NODE(LED_GPIO_NODE_ID, DT_PARENT(DT_ALIAS(led_green))) &&
                 DT_SAME_NODE(LED_GPIO_NODE_ID, DT_PARENT(DT_ALIAS(led_blue))),
             "RGBLED_WIDGET LED aliases must share one gpio-leds parent");

BUILD_ASSERT(!(SHOW_LAYER_CHANGE && SHOW_LAYER_COLORS),
             "CONFIG_RGBLED_WIDGET_SHOW_LAYER_CHANGE and "
             "CONFIG_RGBLED_WIDGET_SHOW_LAYER_COLORS "
             "are mutually exclusive");

// GPIO-based LED device and indices of red/green/blue LEDs inside its DT node
static const struct device *led_dev = DEVICE_DT_GET(LED_GPIO_NODE_ID);
static const uint32_t rgb_idx[] = {DT_NODE_CHILD_IDX(DT_ALIAS(led_red)),
                                   DT_NODE_CHILD_IDX(DT_ALIAS(led_green)),
                                   DT_NODE_CHILD_IDX(DT_ALIAS(led_blue))};

// map from color values to names, for logging
static const char *color_names[] = {"black", "red",     "green", "yellow",
                                    "blue",  "magenta", "cyan",  "white"};

#if SHOW_LAYER_COLORS || SHOW_LAYER_SINGLE_PULSE
static const uint8_t layer_color_idx[] = {
    CONFIG_RGBLED_WIDGET_LAYER_0_COLOR,  CONFIG_RGBLED_WIDGET_LAYER_1_COLOR,
    CONFIG_RGBLED_WIDGET_LAYER_2_COLOR,  CONFIG_RGBLED_WIDGET_LAYER_3_COLOR,
    CONFIG_RGBLED_WIDGET_LAYER_4_COLOR,  CONFIG_RGBLED_WIDGET_LAYER_5_COLOR,
    CONFIG_RGBLED_WIDGET_LAYER_6_COLOR,  CONFIG_RGBLED_WIDGET_LAYER_7_COLOR,
    CONFIG_RGBLED_WIDGET_LAYER_8_COLOR,  CONFIG_RGBLED_WIDGET_LAYER_9_COLOR,
    CONFIG_RGBLED_WIDGET_LAYER_10_COLOR, CONFIG_RGBLED_WIDGET_LAYER_11_COLOR,
    CONFIG_RGBLED_WIDGET_LAYER_12_COLOR, CONFIG_RGBLED_WIDGET_LAYER_13_COLOR,
    CONFIG_RGBLED_WIDGET_LAYER_14_COLOR, CONFIG_RGBLED_WIDGET_LAYER_15_COLOR,
    CONFIG_RGBLED_WIDGET_LAYER_16_COLOR, CONFIG_RGBLED_WIDGET_LAYER_17_COLOR,
    CONFIG_RGBLED_WIDGET_LAYER_18_COLOR, CONFIG_RGBLED_WIDGET_LAYER_19_COLOR,
    CONFIG_RGBLED_WIDGET_LAYER_20_COLOR, CONFIG_RGBLED_WIDGET_LAYER_21_COLOR,
    CONFIG_RGBLED_WIDGET_LAYER_22_COLOR, CONFIG_RGBLED_WIDGET_LAYER_23_COLOR,
    CONFIG_RGBLED_WIDGET_LAYER_24_COLOR, CONFIG_RGBLED_WIDGET_LAYER_25_COLOR,
    CONFIG_RGBLED_WIDGET_LAYER_26_COLOR, CONFIG_RGBLED_WIDGET_LAYER_27_COLOR,
    CONFIG_RGBLED_WIDGET_LAYER_28_COLOR, CONFIG_RGBLED_WIDGET_LAYER_29_COLOR,
    CONFIG_RGBLED_WIDGET_LAYER_30_COLOR, CONFIG_RGBLED_WIDGET_LAYER_31_COLOR,
};
#endif

// log shorthands
#define LOG_CONN_CENTRAL(index, status, color_label)                           \
  LOG_DBG("Profile %d %s, blinking %s", index, status,                         \
          color_names[CONFIG_RGBLED_WIDGET_CONN_COLOR_##color_label])
#define LOG_CONN_PERIPHERAL(status, color_label)                               \
  LOG_DBG("Peripheral %s, blinking %s", status,                                \
          color_names[CONFIG_RGBLED_WIDGET_CONN_COLOR_##color_label])
#define LOG_BATTERY(battery_level, color_label)                                \
  LOG_DBG("Battery level %d, blinking %s", battery_level,                      \
          color_names[CONFIG_RGBLED_WIDGET_BATTERY_COLOR_##color_label])

// a blink work item as specified by the color and duration
struct blink_item {
  uint8_t color;
  bool persistent_color;
  bool layer_pulse;
  uint8_t layer_index;
  uint32_t duration_ms;
  uint32_t sleep_ms;
  bool battery_request;
  uint32_t generation;
  uint32_t pulse_serial;
};

// flag to indicate whether the initial boot up sequence is complete
static bool initialized = false;

// track current color for persistent indicators (layer color)
uint8_t led_current_color = 0;
static K_MUTEX_DEFINE(led_state_lock);
static bool led_sleeping;
static uint32_t led_generation;
static bool connectivity_pending;
static uint32_t connectivity_generation;
#if SHOW_LAYER_CHANGE
static bool layer_indicate_pending;
static uint32_t layer_indicate_generation;
#endif
#if SHOW_LAYER_SINGLE_PULSE
static K_SEM_DEFINE(layer_pulse_changed, 0, 1);
static uint32_t layer_pulse_serial;
#endif

// low-level method to control the LED
/* The caller owns led_state_lock. Never keep it over an indication delay. */
static void set_rgb_leds_locked(uint8_t color) {
  for (uint8_t pos = 0; pos < 3; pos++) {
    uint8_t bit = BIT(pos);
    if ((bit & led_current_color) != (bit & color)) {
      // bits are different, so we need to change one
      if (bit & color) {
        led_on(led_dev, rgb_idx[pos]);
      } else {
        led_off(led_dev, rgb_idx[pos]);
      }
    }
  }
  led_current_color = color;
}

static void set_rgb_leds(uint8_t color, uint32_t duration_ms,
                         uint32_t generation) {
  k_mutex_lock(&led_state_lock, K_FOREVER);
  bool current = !led_sleeping && generation == led_generation;
  if (current) {
    set_rgb_leds_locked(color);
  }
  k_mutex_unlock(&led_state_lock);
  if (current && duration_ms > 0) {
    k_sleep(K_TICKS(
        rgbled_timeout_ticks(duration_ms, CONFIG_SYS_CLOCK_TICKS_PER_SEC)));
  }
}

// define message queue of blink work items, that will be processed by a
// separate thread
K_MSGQ_DEFINE(led_msgq, sizeof(struct blink_item), 16, 1);

static void queue_blink_for_generation(struct blink_item *blink,
                                       uint32_t generation) {
  k_mutex_lock(&led_state_lock, K_FOREVER);
  if (!led_sleeping && generation == led_generation) {
    blink->generation = generation;
#if SHOW_LAYER_SINGLE_PULSE
    if (blink->layer_pulse) {
      blink->pulse_serial = ++layer_pulse_serial;
    }
#endif
    int result = k_msgq_put(&led_msgq, blink, K_NO_WAIT);
#if SHOW_LAYER_SINGLE_PULSE
    if (result == 0 && blink->layer_pulse) {
      k_sem_give(&layer_pulse_changed);
    }
#else
    ARG_UNUSED(result);
#endif
  }
  k_mutex_unlock(&led_state_lock);
}

static uint32_t current_generation(void) {
  k_mutex_lock(&led_state_lock, K_FOREVER);
  uint32_t generation = led_generation;
  k_mutex_unlock(&led_state_lock);
  return generation;
}

static void queue_blink(struct blink_item *blink) {
  queue_blink_for_generation(blink, current_generation());
}

static void indicate_connectivity_internal(uint32_t generation) {
  if (!IS_ENABLED(CONFIG_RGBLED_WIDGET_SHOW_CONNECTION_STATUS)) {
    return;
  }
  struct blink_item blink = {.duration_ms = CONFIG_RGBLED_WIDGET_CONN_BLINK_MS};

#if !IS_ENABLED(CONFIG_ZMK_SPLIT) || IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
#if IS_ENABLED(CONFIG_ZMK_BLE)
  uint8_t profile_index = zmk_ble_active_profile_index();
#endif

  enum zmk_transport transport = zmk_endpoint_get_selected().transport;
  switch (transport) {
  case ZMK_TRANSPORT_USB: // USB connected and selected
#if IS_ENABLED(CONFIG_RGBLED_WIDGET_CONN_SHOW_USB)
    LOG_DBG("USB connected, blinking %s",
            color_names[CONFIG_RGBLED_WIDGET_CONN_COLOR_USB]);
    blink.color = CONFIG_RGBLED_WIDGET_CONN_COLOR_USB;
    break;
#endif
  case ZMK_TRANSPORT_BLE: // BLE connected and selected
#if IS_ENABLED(CONFIG_ZMK_BLE)
    /* A selected BLE endpoint is connected. USB selection alone says
     * nothing about the BLE profile when the USB color is disabled. */
    if (transport == ZMK_TRANSPORT_BLE ||
        zmk_ble_active_profile_is_connected()) {
      LOG_CONN_CENTRAL(profile_index, "connected", CONNECTED);
      blink.color = CONFIG_RGBLED_WIDGET_CONN_COLOR_CONNECTED;
      break;
    }
#endif
  default: // ZMK_TRANSPORT_NONE, neither BLE nor USB connected
#if IS_ENABLED(CONFIG_ZMK_BLE)
    if (zmk_endpoint_get_preferred_transport() != ZMK_TRANSPORT_NONE &&
        zmk_ble_active_profile_is_open()) {
      LOG_CONN_CENTRAL(profile_index, "open", ADVERTISING);
      blink.color = CONFIG_RGBLED_WIDGET_CONN_COLOR_ADVERTISING;
      break;
    }
#endif
    LOG_CONN_CENTRAL(-1, "no endpoints connected", DISCONNECTED);
    blink.color = CONFIG_RGBLED_WIDGET_CONN_COLOR_DISCONNECTED;
    break;
  }
#elif IS_ENABLED(CONFIG_ZMK_SPLIT_BLE)
  if (zmk_split_bt_peripheral_is_connected()) {
    LOG_CONN_PERIPHERAL("connected", CONNECTED);
    blink.color = CONFIG_RGBLED_WIDGET_CONN_COLOR_CONNECTED;
  } else {
    LOG_CONN_PERIPHERAL("not connected", DISCONNECTED);
    blink.color = CONFIG_RGBLED_WIDGET_CONN_COLOR_DISCONNECTED;
  }
#endif

  queue_blink_for_generation(&blink, generation);
}

static int led_output_listener_cb(const zmk_event_t *eh) {
  if (initialized) {
    indicate_connectivity();
  }
  return 0;
}

// debouncing to ignore all but last connectivity event, to prevent repeat
// blinks
static void indicate_connectivity_cb(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(indicate_connectivity_work,
                               indicate_connectivity_cb);

static void indicate_connectivity_cb(struct k_work *work) {
  uint32_t generation = 0;
  k_mutex_lock(&led_state_lock, K_FOREVER);
  bool ready = connectivity_pending && !led_sleeping &&
               connectivity_generation == led_generation &&
               k_work_delayable_remaining_get(&indicate_connectivity_work) == 0;
  if (ready) {
    generation = connectivity_generation;
    connectivity_pending = false;
  }
  k_mutex_unlock(&led_state_lock);
  if (ready) {
    indicate_connectivity_internal(generation);
  }
}

void indicate_connectivity() {
  if (IS_ENABLED(CONFIG_RGBLED_WIDGET_SHOW_CONNECTION_STATUS)) {
    k_mutex_lock(&led_state_lock, K_FOREVER);
    if (!led_sleeping) {
      connectivity_generation = led_generation;
      connectivity_pending = true;
      k_work_reschedule(&indicate_connectivity_work, K_MSEC(16));
    }
    k_mutex_unlock(&led_state_lock);
  }
}

ZMK_LISTENER(led_output_listener, led_output_listener_cb);

#if !IS_ENABLED(CONFIG_ZMK_SPLIT) || IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
// run led_output_listener_cb on endpoint and BLE profile change (on central)
#if IS_ENABLED(CONFIG_RGBLED_WIDGET_CONN_SHOW_USB)
ZMK_SUBSCRIPTION(led_output_listener, zmk_endpoint_changed);
#endif
#if IS_ENABLED(CONFIG_ZMK_BLE)
ZMK_SUBSCRIPTION(led_output_listener, zmk_ble_active_profile_changed);
#endif // IS_ENABLED(CONFIG_ZMK_BLE)
#elif IS_ENABLED(CONFIG_ZMK_SPLIT_BLE)
// run led_output_listener_cb on peripheral status change event
ZMK_SUBSCRIPTION(led_output_listener, zmk_split_peripheral_status_changed);
#endif

#if IS_ENABLED(CONFIG_ZMK_BATTERY_REPORTING)
static inline uint8_t get_battery_color(uint8_t battery_level) {
  if (battery_level == 0) {
    LOG_DBG("Battery level undetermined (zero), blinking %s",
            color_names[CONFIG_RGBLED_WIDGET_BATTERY_COLOR_MISSING]);
    return CONFIG_RGBLED_WIDGET_BATTERY_COLOR_MISSING;
  }
  if (battery_level >= CONFIG_RGBLED_WIDGET_BATTERY_LEVEL_HIGH) {
    LOG_BATTERY(battery_level, HIGH);
    return CONFIG_RGBLED_WIDGET_BATTERY_COLOR_HIGH;
  }
  if (battery_level >= CONFIG_RGBLED_WIDGET_BATTERY_LEVEL_LOW) {
    LOG_BATTERY(battery_level, MEDIUM);
    return CONFIG_RGBLED_WIDGET_BATTERY_COLOR_MEDIUM;
  }
  LOG_BATTERY(battery_level, LOW);
  return CONFIG_RGBLED_WIDGET_BATTERY_COLOR_LOW;
}

static void indicate_battery_internal(uint32_t generation) {
  if (!IS_ENABLED(CONFIG_RGBLED_WIDGET_SHOW_BATTERY_STATUS)) {
    return;
  }
  struct blink_item blink = {.duration_ms =
                                 CONFIG_RGBLED_WIDGET_BATTERY_BLINK_MS};
  int retry = 0;

#if IS_ENABLED(CONFIG_RGBLED_WIDGET_BATTERY_SHOW_SELF) ||                      \
    IS_ENABLED(CONFIG_RGBLED_WIDGET_BATTERY_SHOW_PERIPHERALS)
  uint8_t battery_level = zmk_battery_state_of_charge();
  while (battery_level == 0 && retry++ < 10) {
    k_sleep(K_MSEC(100));
    battery_level = zmk_battery_state_of_charge();
  };

  blink.color = get_battery_color(battery_level);
  queue_blink_for_generation(&blink, generation);
#endif

#if IS_ENABLED(CONFIG_RGBLED_WIDGET_BATTERY_SHOW_PERIPHERALS) ||               \
    IS_ENABLED(CONFIG_RGBLED_WIDGET_BATTERY_SHOW_ONLY_PERIPHERALS)
  for (uint8_t i = 0; i < ZMK_SPLIT_BLE_PERIPHERAL_COUNT; i++) {
    uint8_t peripheral_level;
    int ret =
        zmk_split_central_get_peripheral_battery_level(i, &peripheral_level);
    if (ret == 0) {
      retry = 0;
      while (peripheral_level == 0 &&
             retry++ < (CONFIG_RGBLED_WIDGET_BATTERY_BLINK_MS +
                        CONFIG_RGBLED_WIDGET_INTERVAL_MS) /
                           100) {
        k_sleep(K_MSEC(100));
        zmk_split_central_get_peripheral_battery_level(i, &peripheral_level);
      }

      LOG_DBG("Got battery level for peripheral %d:", i);
      blink.color = get_battery_color(peripheral_level);
      queue_blink_for_generation(&blink, generation);
    } else {
      LOG_ERR("Error looking up battery level for peripheral %d", i);
    }
  }
#endif
}

void indicate_battery(void) {
  if (!IS_ENABLED(CONFIG_RGBLED_WIDGET_SHOW_BATTERY_STATUS)) {
    return;
  }
  struct blink_item request = {.battery_request = true};
  queue_blink(&request);
}

static int led_battery_listener_cb(const zmk_event_t *eh) {
  if (!initialized || !IS_ENABLED(CONFIG_RGBLED_WIDGET_SHOW_BATTERY_STATUS)) {
    return 0;
  }

  // check if we are in critical battery levels at state change, blink if we are
  uint8_t battery_level = as_zmk_battery_state_changed(eh)->state_of_charge;

  if (battery_level > 0 &&
      battery_level <= CONFIG_RGBLED_WIDGET_BATTERY_LEVEL_CRITICAL) {
    LOG_BATTERY(battery_level, CRITICAL);

    struct blink_item blink = {
        .duration_ms = CONFIG_RGBLED_WIDGET_BATTERY_BLINK_MS,
        .color = CONFIG_RGBLED_WIDGET_BATTERY_COLOR_CRITICAL};
    queue_blink(&blink);
  }
  return 0;
}

// run led_battery_listener_cb on battery state change event
ZMK_LISTENER(led_battery_listener, led_battery_listener_cb);
ZMK_SUBSCRIPTION(led_battery_listener, zmk_battery_state_changed);
#endif // IS_ENABLED(CONFIG_ZMK_BATTERY_REPORTING)

uint8_t led_layer_color = 0;
#if SHOW_LAYER_COLORS
void update_layer_color(void) {
  uint8_t index = zmk_keymap_highest_layer_active();

  k_mutex_lock(&led_state_lock, K_FOREVER);
  if (led_layer_color != layer_color_idx[index]) {
    led_layer_color = layer_color_idx[index];
    struct blink_item color = {.color = led_layer_color,
                               .persistent_color = true,
                               .generation = led_generation};
    LOG_DBG("Setting layer color to %s for layer %d",
            color_names[led_layer_color], index);
    if (!led_sleeping) {
      k_msgq_put(&led_msgq, &color, K_NO_WAIT);
    }
  }
  k_mutex_unlock(&led_state_lock);
}

static int led_layer_color_listener_cb(const zmk_event_t *eh) {
  if (initialized) {
    update_layer_color();
  }
  return 0;
}

// run layer_color_listener_cb on layer status change events
ZMK_LISTENER(led_layer_color_listener, led_layer_color_listener_cb);
ZMK_SUBSCRIPTION(led_layer_color_listener, zmk_layer_state_changed);
#endif // SHOW_LAYER_COLORS

/* Sleep invalidation belongs to every role and display mode, including
 * status-only and peripheral builds. Delayable callbacks retain reservations
 * from the old generation instead of adopting the post-wake generation. */
static int led_activity_listener_cb(const zmk_event_t *eh) {
  struct zmk_activity_state_changed *ev = as_zmk_activity_state_changed(eh);
  if (!ev) {
    return 0;
  }
  k_mutex_lock(&led_state_lock, K_FOREVER);
  if (ev->state == ZMK_ACTIVITY_SLEEP) {
    led_sleeping = true;
    ++led_generation;
    connectivity_pending = false;
#if SHOW_LAYER_CHANGE
    layer_indicate_pending = false;
#endif
    set_rgb_leds_locked(0);
#if SHOW_LAYER_SINGLE_PULSE
    k_sem_give(&layer_pulse_changed);
#endif
  } else if (led_sleeping) {
    led_sleeping = false;
#if SHOW_LAYER_COLORS
    set_rgb_leds_locked(led_layer_color);
#endif
  }
  k_mutex_unlock(&led_state_lock);
  return 0;
}
ZMK_LISTENER(led_activity_listener, led_activity_listener_cb);
ZMK_SUBSCRIPTION(led_activity_listener, zmk_activity_state_changed);

#if !IS_ENABLED(CONFIG_ZMK_SPLIT) || IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
#if SHOW_LAYER_SINGLE_PULSE
static uint8_t pulse_layer_index(uint32_t ignored_ids) {
  uint8_t highest = zmk_keymap_highest_layer_active();
  if (!ignored_ids) {
    return highest;
  }
  for (int index = highest; index >= 0; --index) {
    uint8_t id = zmk_keymap_layer_index_to_id((uint8_t)index);
    if (id < 32 && !(ignored_ids & BIT(id)) && zmk_keymap_layer_active(id)) {
      return (uint8_t)index;
    }
  }
  return UINT8_MAX;
}

static void indicate_layer_pulse(uint32_t generation) {
  uint8_t index = pulse_layer_index(CONFIG_RGBLED_WIDGET_LAYER_IGNORE_MASK);
  if (index >= sizeof(layer_color_idx)) {
    return;
  }
  struct blink_item pulse = {.duration_ms = CONFIG_RGBLED_WIDGET_LAYER_BLINK_MS,
                             .color = layer_color_idx[index],
                             .layer_pulse = true,
                             .layer_index = index};
  queue_blink_for_generation(&pulse, generation);
}
#endif

#if !SHOW_LAYER_SINGLE_PULSE
static void indicate_layer_for_generation(uint32_t generation) {
  uint8_t index = zmk_keymap_highest_layer_active();
  struct blink_item blink = {.duration_ms = CONFIG_RGBLED_WIDGET_LAYER_BLINK_MS,
                             .color = CONFIG_RGBLED_WIDGET_LAYER_COLOR,
                             .sleep_ms = CONFIG_RGBLED_WIDGET_LAYER_BLINK_MS};
  struct blink_item last_blink = {.duration_ms =
                                      CONFIG_RGBLED_WIDGET_LAYER_BLINK_MS,
                                  .color = CONFIG_RGBLED_WIDGET_LAYER_COLOR};
  LOG_DBG("Blinking %d times %s for layer change", index,
          color_names[CONFIG_RGBLED_WIDGET_LAYER_COLOR]);

  for (int i = 0; i < index; i++) {
    if (i < index - 1) {
      queue_blink_for_generation(&blink, generation);
    } else {
      queue_blink_for_generation(&last_blink, generation);
    }
  }
}
#endif

void indicate_layer(void) {
#if SHOW_LAYER_SINGLE_PULSE
  indicate_layer_pulse(current_generation());
#else
  indicate_layer_for_generation(current_generation());
#endif
}
#endif // !IS_ENABLED(CONFIG_ZMK_SPLIT) ||
       // IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)

#if SHOW_LAYER_CHANGE
static void indicate_layer_cb(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(layer_indicate_work, indicate_layer_cb);
#if SHOW_LAYER_SINGLE_PULSE
static uint8_t last_indicated_layer;
#endif

static void indicate_layer_cb(struct k_work *work) {
  uint32_t generation = 0;
  k_mutex_lock(&led_state_lock, K_FOREVER);
  /* A callback already submitted before rescheduling must not consume the
   * newer request before its debounce expires, or emit it a second time. */
  bool ready = layer_indicate_pending && !led_sleeping &&
               layer_indicate_generation == led_generation &&
               k_work_delayable_remaining_get(&layer_indicate_work) == 0;
  if (ready) {
    generation = layer_indicate_generation;
    layer_indicate_pending = false;
  }
  k_mutex_unlock(&led_state_lock);
  if (ready) {
    /* Sleep may start after the claim: retain its original generation
     * through layer lookup and queue admission. */
#if SHOW_LAYER_SINGLE_PULSE
    indicate_layer_pulse(generation);
#else
    indicate_layer_for_generation(generation);
#endif
  }
}

static int led_layer_listener_cb(const zmk_event_t *eh) {
#if SHOW_LAYER_SINGLE_PULSE
  uint8_t index = pulse_layer_index(CONFIG_RGBLED_WIDGET_LAYER_IGNORE_MASK);
  k_mutex_lock(&led_state_lock, K_FOREVER);
  if (initialized && index != last_indicated_layer) {
    last_indicated_layer = index;
    if (!led_sleeping && index != UINT8_MAX) {
      layer_indicate_generation = led_generation;
      layer_indicate_pending = true;
      k_work_reschedule(&layer_indicate_work,
                        K_MSEC(CONFIG_RGBLED_WIDGET_LAYER_DEBOUNCE_MS));
    }
  }
  k_mutex_unlock(&led_state_lock);
#else
  // ignore if not initialized yet or layer off events
  if (initialized && as_zmk_layer_state_changed(eh)->state) {
    k_mutex_lock(&led_state_lock, K_FOREVER);
    if (!led_sleeping) {
      layer_indicate_generation = led_generation;
      layer_indicate_pending = true;
      k_work_reschedule(&layer_indicate_work,
                        K_MSEC(CONFIG_RGBLED_WIDGET_LAYER_DEBOUNCE_MS));
    }
    k_mutex_unlock(&led_state_lock);
  }
#endif
  return 0;
}

ZMK_LISTENER(led_layer_listener, led_layer_listener_cb);
ZMK_SUBSCRIPTION(led_layer_listener, zmk_layer_state_changed);
#endif // SHOW_LAYER_CHANGE

extern void led_process_thread(void *d0, void *d1, void *d2) {
  ARG_UNUSED(d0);
  ARG_UNUSED(d1);
  ARG_UNUSED(d2);

  while (true) {
    // wait until a blink item is received and process it
    struct blink_item blink;
    k_msgq_get(&led_msgq, &blink, K_FOREVER);
    k_mutex_lock(&led_state_lock, K_FOREVER);
    bool current = !led_sleeping && blink.generation == led_generation;
    uint8_t current_color = led_current_color;
    uint8_t layer_color = led_layer_color;
    k_mutex_unlock(&led_state_lock);
    if (!current) {
      continue;
    }
#if SHOW_LAYER_SINGLE_PULSE
    if (blink.layer_pulse) {
      k_mutex_lock(&led_state_lock, K_FOREVER);
      if (led_sleeping || blink.generation != led_generation ||
          blink.pulse_serial != layer_pulse_serial ||
          blink.layer_index !=
              pulse_layer_index(CONFIG_RGBLED_WIDGET_LAYER_IGNORE_MASK)) {
        k_mutex_unlock(&led_state_lock);
        continue;
      }
      /* Reset under the same lock as producers: a later update leaves
       * a semaphore token even if it arrives before this wait starts. */
      k_sem_reset(&layer_pulse_changed);
      set_rgb_leds_locked(blink.color);
      k_mutex_unlock(&led_state_lock);
      k_sem_take(&layer_pulse_changed,
                 K_TICKS(rgbled_timeout_ticks(blink.duration_ms,
                                              CONFIG_SYS_CLOCK_TICKS_PER_SEC)));
      set_rgb_leds(0, 0, blink.generation);
      continue;
    }
#endif
#if IS_ENABLED(CONFIG_ZMK_BATTERY_REPORTING)
    if (blink.battery_request) {
      indicate_battery_internal(blink.generation);
      continue;
    }
#endif
    if (blink.duration_ms > 0) {
      LOG_DBG("Got a blink item from msgq, color %d, duration %d", blink.color,
              blink.duration_ms);

      // Blink the leds, using a separation blink if necessary
      if (blink.color == current_color && blink.color > 0) {
        set_rgb_leds(0, CONFIG_RGBLED_WIDGET_INTERVAL_MS, blink.generation);
      }
      set_rgb_leds(blink.color, blink.duration_ms, blink.generation);
      if (blink.color == layer_color && blink.color > 0) {
        set_rgb_leds(0, CONFIG_RGBLED_WIDGET_INTERVAL_MS, blink.generation);
      }
      // wait interval before processing another blink
      k_mutex_lock(&led_state_lock, K_FOREVER);
      layer_color = led_layer_color;
      k_mutex_unlock(&led_state_lock);
      set_rgb_leds(layer_color,
                   blink.sleep_ms > 0 ? blink.sleep_ms
                                      : CONFIG_RGBLED_WIDGET_INTERVAL_MS,
                   blink.generation);

    } else {
      // Layer updates are notifications: a saturated queue may contain
      // older colors while led_layer_color already holds the latest target.
      uint8_t color = blink.persistent_color ? layer_color : blink.color;
      LOG_DBG("Got a layer color item from msgq, color %d", color);
      set_rgb_leds(color, 0, blink.generation);
    }
  }
}

// define led_process_thread with stack size 1024, start running it 100 ms after
// boot
K_THREAD_DEFINE(led_process_tid, 1024, led_process_thread, NULL, NULL, NULL,
                K_LOWEST_APPLICATION_THREAD_PRIO, 0, 100);

extern void led_init_thread(void *d0, void *d1, void *d2) {
  ARG_UNUSED(d0);
  ARG_UNUSED(d1);
  ARG_UNUSED(d2);

#if IS_ENABLED(CONFIG_ZMK_BATTERY_REPORTING)
  // check and indicate battery level on thread start
  LOG_DBG("Indicating initial battery status");

  if (IS_ENABLED(CONFIG_RGBLED_WIDGET_SHOW_BATTERY_STATUS)) {
    indicate_battery_internal(current_generation());

    // Wait only when a battery indication was requested.
    k_sleep(K_MSEC(CONFIG_RGBLED_WIDGET_BATTERY_BLINK_MS +
                   CONFIG_RGBLED_WIDGET_INTERVAL_MS));
  }
#endif // IS_ENABLED(CONFIG_ZMK_BATTERY_REPORTING)

  // check and indicate current profile or peripheral connectivity status
  LOG_DBG("Indicating initial connectivity status");
  indicate_connectivity();

#if SHOW_LAYER_COLORS
  LOG_DBG("Setting initial layer color");
  update_layer_color();
#endif // SHOW_LAYER_COLORS

#if SHOW_LAYER_SINGLE_PULSE
  k_mutex_lock(&led_state_lock, K_FOREVER);
  last_indicated_layer =
      pulse_layer_index(CONFIG_RGBLED_WIDGET_LAYER_IGNORE_MASK);
  initialized = true;
  k_mutex_unlock(&led_state_lock);
#else
  initialized = true;
#endif
  LOG_INF("RGB indicator ready");
}

// run init thread on boot for initial battery+output checks
K_THREAD_DEFINE(led_init_tid, 1024, led_init_thread, NULL, NULL, NULL,
                K_LOWEST_APPLICATION_THREAD_PRIO, 0, 200);
