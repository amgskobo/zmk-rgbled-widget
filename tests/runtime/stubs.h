/* SPDX-License-Identifier: MIT */
#pragma once
#include <assert.h>
#include <errno.h>
#include <setjmp.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define IS_ENABLED(x) (x)
#ifndef CONFIG_RGBLED_WIDGET_SHOW_CONNECTION_STATUS
#define CONFIG_RGBLED_WIDGET_SHOW_CONNECTION_STATUS 1
#endif
#ifndef CONFIG_RGBLED_WIDGET_SHOW_BATTERY_STATUS
#define CONFIG_RGBLED_WIDGET_SHOW_BATTERY_STATUS 1
#endif
#ifndef CONFIG_RGBLED_WIDGET_LAYER_SINGLE_PULSE
#define CONFIG_RGBLED_WIDGET_LAYER_SINGLE_PULSE 0
#endif
#ifndef CONFIG_RGBLED_WIDGET_LAYER_IGNORE_MASK
#define CONFIG_RGBLED_WIDGET_LAYER_IGNORE_MASK 0
#endif
#define BIT(x) (1U << (x))
#define BUILD_ASSERT(x, msg) _Static_assert(x, msg)
#define ARG_UNUSED(x) ((void)(x))
#define DT_COMPAT_GET_ANY_STATUS_OKAY(x) 0
#define DT_NODE_EXISTS(x) 1
#define led_red 0
#define led_green 1
#define led_blue 2
#define DT_ALIAS(x) (x)
#define DT_PARENT(x) 0
#define DT_NODE_HAS_COMPAT(x, compat) 1
#define DT_SAME_NODE(x, y) ((x) == (y))
#define DT_NODE_CHILD_IDX(x) (x)
struct device {
  const void *config;
};
static struct device fake_led_device;
#define DEVICE_DT_GET(x) (&fake_led_device)
#define LOG_MODULE_REGISTER(...)
#define LOG_DBG(...)                                                           \
  do {                                                                         \
    if (0)                                                                     \
      fprintf(stderr, __VA_ARGS__);                                            \
  } while (0)
#define LOG_INF(...) LOG_DBG(__VA_ARGS__)
#define LOG_ERR(...) LOG_DBG(__VA_ARGS__)
#define ZMK_LISTENER(n, cb)
#define ZMK_SUBSCRIPTION(n, e)
#define K_MSEC(x) (x)
#define K_TICKS(x) (x)
#define CONFIG_SYS_CLOCK_TICKS_PER_SEC 1000
#define K_NO_WAIT 0
#define K_FOREVER (-1)
#define K_LOWEST_APPLICATION_THREAD_PRIO 0
#define K_THREAD_DEFINE(...)
struct k_mutex {
  unsigned depth;
};
#define K_MUTEX_DEFINE(n) struct k_mutex n
static unsigned mutex_depth;
static void (*mutex_unlock_hook)(void);
static int k_mutex_lock(struct k_mutex *m, int timeout) {
  assert(timeout == K_FOREVER && !m->depth);
  m->depth++;
  mutex_depth++;
  return 0;
}
static int k_mutex_unlock(struct k_mutex *m) {
  assert(m->depth == 1 && mutex_depth);
  m->depth--;
  mutex_depth--;
  if (mutex_unlock_hook) {
    void (*hook)(void) = mutex_unlock_hook;
    mutex_unlock_hook = NULL;
    hook();
  }
  return 0;
}
struct k_msgq {
  unsigned size, count, head;
  unsigned char data[16][32];
};
#define K_MSGQ_DEFINE(n, size_, count_, align_)                                \
  static struct k_msgq n = {.size = size_}
struct k_work {
  int unused;
};
struct k_work_delayable {
  void (*handler)(struct k_work *);
  unsigned due, schedules;
};
#define K_WORK_DELAYABLE_DEFINE(name, cb)                                      \
  struct k_work_delayable name = {.handler = cb}
static unsigned now_ms, gpio_bits, edges, sleeps;
static struct {
  unsigned at, channel, on;
} gpio_trace[512];
static unsigned sleep_trace[128];
static jmp_buf worker_done;
static void (*sleep_hook)(void);
static int led_on(const struct device *dev, unsigned index) {
  assert(dev == &fake_led_device && index < 3 && edges < 512);
  gpio_trace[edges].at = now_ms;
  gpio_trace[edges].channel = index;
  gpio_trace[edges++].on = 1;
  gpio_bits |= BIT(index);
  return 0;
}
static int led_off(const struct device *dev, unsigned index) {
  assert(dev == &fake_led_device && index < 3 && edges < 512);
  gpio_trace[edges].at = now_ms;
  gpio_trace[edges].channel = index;
  gpio_trace[edges++].on = 0;
  gpio_bits &= ~BIT(index);
  return 0;
}
static int k_sleep(unsigned ms) {
  assert(sleeps < 128 && !mutex_depth);
  sleep_trace[sleeps++] = ms;
  now_ms += ms;
  if (sleep_hook) {
    void (*hook)(void) = sleep_hook;
    sleep_hook = NULL;
    hook();
  }
  return 0;
}
struct k_sem {
  unsigned count;
};
#define K_SEM_DEFINE(name, initial, limit)                                     \
  struct k_sem name = {.count = initial}
static void k_sem_reset(struct k_sem *sem) { sem->count = 0; }
static void k_sem_give(struct k_sem *sem) { sem->count = 1; }
static int k_sem_take(struct k_sem *sem, unsigned timeout) {
  if (sem->count) {
    sem->count = 0;
    return 0;
  }
  if (timeout)
    k_sleep(timeout);
  if (sem->count) {
    sem->count = 0;
    return 0;
  }
  return -EAGAIN;
}
static int k_msgq_put(struct k_msgq *q, const void *item, int timeout) {
  assert(timeout == K_NO_WAIT && q->size <= 32);
  if (q->count == 16)
    return -ENOMSG;
  memcpy(q->data[(q->head + q->count++) % 16], item, q->size);
  return 0;
}
static int k_msgq_get(struct k_msgq *q, void *item, int timeout) {
  assert(timeout == K_FOREVER);
  if (!q->count)
    longjmp(worker_done, 1);
  memcpy(item, q->data[q->head], q->size);
  q->head = (q->head + 1) % 16;
  q->count--;
  return 0;
}
static int k_work_reschedule(struct k_work_delayable *w, unsigned ms) {
  assert(w->handler);
  w->due = now_ms + ms;
  w->schedules++;
  return 0;
}
static unsigned
k_work_delayable_remaining_get(const struct k_work_delayable *w) {
  return w->due > now_ms ? w->due - now_ms : 0;
}
static void k_work_init_delayable(struct k_work_delayable *w,
                                  void (*cb)(struct k_work *)) {
  w->handler = cb;
}
enum zmk_transport { ZMK_TRANSPORT_NONE, ZMK_TRANSPORT_USB, ZMK_TRANSPORT_BLE };
struct zmk_endpoint_instance {
  enum zmk_transport transport;
};
static enum zmk_transport selected, preferred;
static bool profile_open, profile_connected, peripheral_connected;
static unsigned layer, battery, battery_reads, battery_ready_after;
static uint32_t extra_active_layers;
static bool reorder_layers;
static int peripheral_error[2];
static unsigned peripheral_battery[2], peripheral_reads[2],
    peripheral_ready_after[2];
static struct zmk_endpoint_instance zmk_endpoint_get_selected(void) {
  return (struct zmk_endpoint_instance){selected};
}
static enum zmk_transport zmk_endpoint_get_preferred_transport(void) {
  return preferred;
}
static uint8_t zmk_ble_active_profile_index(void) { return 2; }
static bool zmk_ble_active_profile_is_open(void) { return profile_open; }
static bool zmk_ble_active_profile_is_connected(void) {
  return profile_connected;
}
static bool zmk_split_bt_peripheral_is_connected(void) {
  return peripheral_connected;
}
static uint8_t zmk_keymap_highest_layer_active(void) { return layer; }
static uint8_t zmk_keymap_layer_index_to_id(uint8_t index) {
  return reorder_layers && index == 3   ? 9
         : reorder_layers && index == 9 ? 3
                                        : index;
}
static bool zmk_keymap_layer_active(uint8_t id) {
  return id == 0 || id == zmk_keymap_layer_index_to_id((uint8_t)layer) ||
         (extra_active_layers & BIT(id));
}
static uint8_t zmk_battery_state_of_charge(void) {
  return battery_reads++ < battery_ready_after ? 0 : battery;
}
static int zmk_split_central_get_peripheral_battery_level(unsigned i,
                                                          uint8_t *level) {
  assert(i < 2);
  *level = peripheral_reads[i]++ < peripheral_ready_after[i]
               ? 0
               : peripheral_battery[i];
  return peripheral_error[i];
}
enum { ZMK_ACTIVITY_ACTIVE, ZMK_ACTIVITY_IDLE, ZMK_ACTIVITY_SLEEP };
typedef struct {
  int type;
  uint8_t value;
} zmk_event_t;
struct zmk_battery_state_changed {
  uint8_t state_of_charge;
};
struct zmk_layer_state_changed {
  bool state;
};
struct zmk_activity_state_changed {
  int state;
};
static struct zmk_battery_state_changed *
as_zmk_battery_state_changed(const zmk_event_t *e) {
  static struct zmk_battery_state_changed v;
  v.state_of_charge = e->value;
  return &v;
}
static struct zmk_layer_state_changed *
as_zmk_layer_state_changed(const zmk_event_t *e) {
  static struct zmk_layer_state_changed v;
  v.state = e->value;
  return &v;
}
static struct zmk_activity_state_changed *
as_zmk_activity_state_changed(const zmk_event_t *e) {
  static struct zmk_activity_state_changed v;
  v.state = e->value;
  return e->type == 1 ? &v : NULL;
}
struct zmk_behavior_binding {
  const char *behavior_dev;
};
struct zmk_behavior_binding_event {
  int unused;
};
struct behavior_driver_api {
  int (*binding_pressed)(struct zmk_behavior_binding *,
                         struct zmk_behavior_binding_event);
  int (*binding_released)(struct zmk_behavior_binding *,
                          struct zmk_behavior_binding_event);
  int locality;
};
#define BEHAVIOR_LOCALITY_GLOBAL 1
#define ZMK_BEHAVIOR_OPAQUE 0
#define DT_INST_FOREACH_STATUS_OKAY(...)
static struct device binding_device;
static const struct device *zmk_behavior_get_binding(const char *name) {
  assert(name);
  return &binding_device;
}
