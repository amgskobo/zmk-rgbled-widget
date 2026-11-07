# LED indicators using an RGB LED

## Reviewed fork

This fork's reviewed source is on `codex/rgbled-widget-review` in
[`amgskobo/zmk-rgbled-widget`](https://github.com/amgskobo/zmk-rgbled-widget/tree/codex/rgbled-widget-review).
Select that repository URL and revision in the module's west project entry.
Keep the module's documented imports enabled where required. The original
upstream installation examples below describe the upstream repository.

> [!IMPORTANT]
> This module uses a versioning scheme that is compatible with ZMK versions.
> As a general rule, the `main` branch is targeting compatibility with ZMK's `main`.
>
> **If you have build failures with ZMK's latest release (like `v0.3`) make sure to [use the corresponding revision](#installation) for `zmk-rgbled-widget` in your `west.yml`**.

This is a [ZMK module](https://zmk.dev/docs/features/modules) containing a simple widget that utilizes a (typically built-in) RGB LED controlled by three separate GPIOs.
It is used to indicate battery level and BLE connection status in a minimalist way.

## Features

<details>
  <summary>Short video demo</summary>
  See below video for a short demo, running through power on, profile switching and power offs.

  https://github.com/caksoylar/zmk-rgbled-widget/assets/7876996/cfd89dd1-ff24-4a33-8563-2fdad2a828d4
</details>

### Battery status

- Set `CONFIG_RGBLED_WIDGET_SHOW_BATTERY_STATUS=n` to suppress boot, critical
  battery and on-demand battery indications. The default is `y`.
- Blink 🟢/🟡/🔴 on boot depending on battery level, with thresholds [set](#configuration-details) by `CONFIG_RGBLED_WIDGET_BATTERY_LEVEL_HIGH` and `CONFIG_RGBLED_WIDGET_BATTERY_LEVEL_LOW`
  - See [options](#battery-levels-for-splits) for showing battery levels for splits
- Blink 🔴 on every battery level change if below critical battery level (`CONFIG_RGBLED_WIDGET_BATTERY_LEVEL_CRITICAL`)

### Connection status

- Set `CONFIG_RGBLED_WIDGET_SHOW_CONNECTION_STATUS=n` to suppress boot,
  endpoint/profile changes and on-demand connection indications. The default is `y`.
- Blink 🔵 for connected, 🟡 for open (advertising), 🔴 for disconnected profiles on boot after the battery blink, and following every BT profile switch (only on central side for splits)
  - Enable `CONFIG_RGBLED_WIDGET_CONN_SHOW_USB` to blink cyan if USB currently has priority over BLE, instead of above
  - With that option disabled, selecting USB still shows the active BLE
    profile's actual connected/open/disconnected state. USB selection alone
    does not make a disconnected BLE profile show the connected color.
- Blink 🔵 for connected, 🔴 for disconnected on peripheral side of splits

### Layer state

You can pick one of the following methods (off by default) to indicate the highest active layer:

- Enable `CONFIG_RGBLED_WIDGET_SHOW_LAYER_CHANGE` to show the highest active layer on every layer activation
  using a sequence of N cyan color blinks, where N is the zero-based index of the layer, or
- Enable `CONFIG_RGBLED_WIDGET_SHOW_LAYER_COLORS` to assign each layer its own color, which will remain on while that layer is the highest active layer

These layer indicators will only be active on the central part of a split keyboard, since peripheral parts aren't aware of the layer information.

For one brief layer-colored indication at each change, enable
`CONFIG_RGBLED_WIDGET_SHOW_LAYER_CHANGE=y` and
`CONFIG_RGBLED_WIDGET_LAYER_SINGLE_PULSE=y`, with
`CONFIG_RGBLED_WIDGET_SHOW_LAYER_COLORS=n`. This uses the per-layer color table,
including layer0, and turns off after `CONFIG_RGBLED_WIDGET_LAYER_BLINK_MS`.
Both activation and release are considered when the highest layer changes;
events that leave it unchanged do not schedule another pulse. Debouncing keeps
the latest layer, and queued pulses for an obsolete layer are discarded.
Sleep cancels old indications; boot and wake alone do not produce a layer pulse.
This includes debounced changes still waiting to be displayed. Layer changes
while asleep do not request a pulse; a new change after wake uses normal debounce.
`CONFIG_RGBLED_WIDGET_LAYER_IGNORE_MASK` excludes stable layer IDs from this
single-pulse selection. For example, `0x200` ignores automatic mouse layer ID9.
Entering or leaving that layer alone is silent; a manual layer below it still
appears, and a queued indication remains valid when only the ignored layer
changes. Reordered layers are mapped from their display index to stable ID
before testing the mask. If no eligible layer is active, no pulse is queued.
The pulse wait can be interrupted by a newer layer or sleep. Only the latest
queued layer request is eligible, and each new indication starts its own duration.
Disable both status options above for layer-change-only output. A zero status
duration is not an off switch: generic zero-duration items set a steady color.

> [!TIP]
> Also see [below](#showing-status-on-demand) for keymap behaviors you can use to show the battery and connection status on demand.

## Installation

To use, first add this module to your `config/west.yml` by adding a new entry to `projects`:

```yaml west.yml
manifest:
  remotes:
    - name: zmkfirmware
      url-base: https://github.com/zmkfirmware
  projects:
    - name: zmk
      remote: zmkfirmware
      revision: v0.3           # Your ZMK version
      import: app/west.yml
    - name: zmk-rgbled-widget  # <-- new entry
      url: https://github.com/caksoylar/zmk-rgbled-widget
      revision: v0.3           # MUST match your ZMK version!
  self:
    path: config
```

For more information, including instructions for building locally, check out the ZMK docs on [building with modules](https://zmk.dev/docs/features/modules#building-with-modules).

Then, if you are using one of the boards supported by the [`rgbled_adapter`](boards/shields/rgbled_adapter) shield such as Xiao BLE,
just add the `rgbled_adapter` as an additional shield to your build, e.g. in `build.yaml`:

```yaml build.yaml
---
include:
  - board: xiao_ble//zmk
    shield: hummingbird rgbled_adapter
```

For other keyboards, see the ["Adding support" section](#adding-support-in-custom-boardsshields) below.

## Showing status on demand

This module also defines keymap [behaviors](https://zmk.dev/docs/keymaps/behaviors) to let you show battery or connection status on demand:

```dts
#include <behaviors/rgbled_widget.dtsi>  // needed to use the behaviors

/ {
    keymap {
        ...
        some_layer {
            bindings = <
                ...
                &ind_bat  // indicate battery level
                &ind_con  // indicate connectivity status
                ...
            >;
        };
    };
};
```

When you invoke the behavior by pressing the corresponding key (or combo), it will trigger the corresponding indicator on the LED.
This will happen on all keyboard parts for split keyboards, so make sure to flash firmware to all parts after enabling.

Battery requests enter the bounded LED queue immediately. The LED thread performs
the existing battery retries; the key-processing caller does not wait for them.
As with other indications, a full queue drops a new request. Boot initialization
keeps its separate battery-first sequence. Connectivity work is initialized
statically and accepts requests before the LED thread starts.

Sleep requests output off and invalidates older queued, running and deferred
indications in every display mode and split role, including status-only and
peripheral builds. Requests made while sleeping are suppressed. A new request
after ACTIVE or IDLE uses the ordinary debounce; an older submitted callback
cannot consume that new request early or twice. With persistent layer colors,
layer changes while asleep update the saved target and wake restores that latest
color. Other modes stay off until a new indication. No old blink, connectivity
reservation or battery retry may replace the restored output after wake.
Indication delays never hold the output-state mutex. This describes the widget's
LED API requests; physical GPIO retention and power consumption require hardware
acceptance.

> [!NOTE]
> The behaviors can be used even when you use split keyboards with different controllers that don't all support the widget.
> Make sure that you use the `rgbled_adapter` shield (or enable `CONFIG_RGBLED_WIDGET` if not using the adapter) _only_ for the keyboard parts that support it.

## Battery levels for splits

For split keyboards, each part will indicate its own battery level with a single battery blink, by default.
However, for some scenarios like keyboards with dongles and no RGB LED on the peripherals, you might want the central part to show the battery levels of peripherals too.
This can be done by enabling one of the below settings:

- `CONFIG_RGBLED_WIDGET_BATTERY_SHOW_PERIPHERALS`: Blink for battery level of self and then the peripherals, in order
- `CONFIG_RGBLED_WIDGET_BATTERY_SHOW_ONLY_PERIPHERALS`: Blink for battery level of only the peripherals, in order

These two settings only apply to split central parts.
The order of blinks for peripherals is determined by the initial pairing order for the split parts.
If a part is currently disconnected, a magenta/purple ([configurable](#configuration-details)) blink will be displayed.

## Configuration details

<details>
<summary>General</summary>

| Name                               | Description                                    | Default |
| ---------------------------------- | ---------------------------------------------- | ------- |
| `CONFIG_RGBLED_WIDGET_INTERVAL_MS` | Minimum wait duration between two blinks in ms | 500     |

</details>

<details>
<summary>Battery-related</summary>

| Name                                          | Description                                                           | Default       |
| --------------------------------------------- | --------------------------------------------------------------------- | ------------- |
| `CONFIG_RGBLED_WIDGET_BATTERY_BLINK_MS`       | Duration of battery level blink in ms                                 | 2000          |
| `CONFIG_RGBLED_WIDGET_BATTERY_LEVEL_HIGH`     | High battery level percentage                                         | 80            |
| `CONFIG_RGBLED_WIDGET_BATTERY_LEVEL_LOW`      | Low battery level percentage                                          | 20            |
| `CONFIG_RGBLED_WIDGET_BATTERY_LEVEL_CRITICAL` | Critical battery level percentage, blink periodically if under        | 5             |
| `CONFIG_RGBLED_WIDGET_BATTERY_COLOR_HIGH`     | Color for high battery level (above `LEVEL_HIGH`)                     | Green (`2`)   |
| `CONFIG_RGBLED_WIDGET_BATTERY_COLOR_MEDIUM`   | Color for medium battery level (between `LEVEL_LOW` and `LEVEL_HIGH`) | Yellow (`3`)  |
| `CONFIG_RGBLED_WIDGET_BATTERY_COLOR_LOW`      | Color for low battery level (below `LEVEL_LOW`)                       | Red (`1`)     |
| `CONFIG_RGBLED_WIDGET_BATTERY_COLOR_CRITICAL` | Color for critical battery level (below `LEVEL_CRITICAL`)             | Red (`1`)     |
| `CONFIG_RGBLED_WIDGET_BATTERY_COLOR_MISSING`  | Color for battery not detected, or peripheral disconnected            | Magenta (`5`) |

Only one of the options below can be enabled.
The non-default ones (second and third below) only work on central parts of splits.

| Name                                                 | Description                                             | Default |
| ---------------------------------------------------- | ------------------------------------------------------- | ------- |
| `CONFIG_RGBLED_WIDGET_BATTERY_SHOW_SELF`             | Indicate battery level from self only                   | `n`     |
| `CONFIG_RGBLED_WIDGET_BATTERY_SHOW_PERIPHERALS`      | On a split central, also show peripheral battery levels | `n`     |
| `CONFIG_RGBLED_WIDGET_BATTERY_SHOW_ONLY_PERIPHERALS` | On a split central, show only peripheral battery levels | `n`     |

</details>

<details>
<summary>Connectivity-related</summary>

| Name                                           | Description                                                 | Default      |
| ---------------------------------------------- | ----------------------------------------------------------- | ------------ |
| `CONFIG_RGBLED_WIDGET_CONN_BLINK_MS`           | Duration of BLE connection status blink in ms               | 1000         |
| `CONFIG_RGBLED_WIDGET_CONN_SHOW_USB`           | Show USB indicator instead of BLE status if it has priority | `n`          |
| `CONFIG_RGBLED_WIDGET_CONN_COLOR_CONNECTED`    | Color for connected BLE connection status                   | Blue (`4`)   |
| `CONFIG_RGBLED_WIDGET_CONN_COLOR_ADVERTISING`  | Color for advertising BLE connection status                 | Yellow (`3`) |
| `CONFIG_RGBLED_WIDGET_CONN_COLOR_DISCONNECTED` | Color for disconnected BLE connection status                | Red (`1`)    |
| `CONFIG_RGBLED_WIDGET_CONN_COLOR_USB`          | Color for USB endpoint active                               | Cyan (`6`)   |

</details>

<details>
<summary>Layers-related</summary>

Layer indicator only works on non-splits and central parts of splits.

Below settings enable and configure the sequence-based layer indicator.

| Name                                     | Description                                                                  | Default    |
| ---------------------------------------- | ---------------------------------------------------------------------------- | ---------- |
| `CONFIG_RGBLED_WIDGET_SHOW_LAYER_CHANGE` | Indicate highest active layer on each layer change with a sequence of blinks | `n`        |
| `CONFIG_RGBLED_WIDGET_LAYER_BLINK_MS`    | Blink and wait duration for layer indicator                                  | 100        |
| `CONFIG_RGBLED_WIDGET_LAYER_COLOR`       | Color to use for layer indicator                                             | Cyan (`6`) |
| `CONFIG_RGBLED_WIDGET_LAYER_DEBOUNCE_MS` | Wait duration after a layer change before showing the highest active layer   | 100        |

Below settings enable and configure the color-based layer indicator.

| Name                                     | Description                                                                | Default       |
| ---------------------------------------- | -------------------------------------------------------------------------- | ------------- |
| `CONFIG_RGBLED_WIDGET_SHOW_LAYER_COLORS` | Indicate highest active layer with a constant configurable color per layer | `n`           |
| `CONFIG_RGBLED_WIDGET_LAYER_0_COLOR`     | Color to use for the base layer                                            | Black (`0`)   |
| `CONFIG_RGBLED_WIDGET_LAYER_1_COLOR`     | Color to use for layer 1                                                   | Red (`1`)     |
| `CONFIG_RGBLED_WIDGET_LAYER_2_COLOR`     | Color to use for layer 2                                                   | Green (`2`)   |
| `CONFIG_RGBLED_WIDGET_LAYER_3_COLOR`     | Color to use for layer 3                                                   | Yellow (`3`)  |
| `CONFIG_RGBLED_WIDGET_LAYER_4_COLOR`     | Color to use for layer 4                                                   | Blue (`4`)    |
| `CONFIG_RGBLED_WIDGET_LAYER_5_COLOR`     | Color to use for layer 5                                                   | Magenta (`5`) |
| `CONFIG_RGBLED_WIDGET_LAYER_6_COLOR`     | Color to use for layer 6                                                   | Cyan (`6`)    |
| `CONFIG_RGBLED_WIDGET_LAYER_7_COLOR`     | Color to use for layer 7                                                   | White (`7`)   |
| `CONFIG_RGBLED_WIDGET_LAYER_xx_COLOR`    | Color to use for layer xx (change xx to the layer number to change)        | Black (`0`)   |

</details>

<details>
<summary>Mapping for color values</summary>
Color settings use the following integer values:

| Color        | Value |
| ------------ | ----- |
| Black (none) | `0`   |
| Red          | `1`   |
| Green        | `2`   |
| Yellow       | `3`   |
| Blue         | `4`   |
| Magenta      | `5`   |
| Cyan         | `6`   |
| White        | `7`   |

</details>

You can add these settings to your conf file to modify the config values. You can use a `config/rgbled_adapter.conf` file in your ZMK config repo if you use the adapter shield as mentioned above, or `config/<keyboard>.conf`. E.g. in `config/rgbled_adapter.conf`:

```ini
CONFIG_RGBLED_WIDGET_INTERVAL_MS=250
CONFIG_RGBLED_WIDGET_BATTERY_LEVEL_HIGH=50
CONFIG_RGBLED_WIDGET_BATTERY_LEVEL_CRITICAL=10
```

## Adding support in custom boards/shields

To be able to use this widget, you need three LEDs controlled by GPIOs (_not_ smart LEDs), ideally red, green and blue colors.
Once you have these LED definitions in your board/shield, simply set the appropriate `aliases` to the RGB LED node labels.
All three aliases must share one `gpio-leds` parent. The widget selects that
parent directly, so additional LED devices on the board remain independent.
Child indices use the LED API's full uint32 width. Mixed parents or a parent
without `gpio-leds` compatibility are rejected at compilation.

Output is best effort through the GPIO LED driver. An unready device is not
called. Each channel's cached color changes only after that channel's API call
succeeds; a failed channel remains eligible when the same color is requested
again. Other successful channels retain their state. This also applies to an
off request during sleep. There is no background driver-recovery polling: a
later indication or activity request retries the required change. Driver failure
can therefore leave a physical LED on until a successful off request; the cache
records confirmed API operations, not a measurement of the LED.

As an example, here is a definition for three LEDs connected to VCC and separate GPIOs for a nRF52840 controller:

```dts
/ {
    aliases {
        led-red = &led0;
        led-green = &led1;
        led-blue = &led2;
    };

    leds {
        compatible = "gpio-leds";
        status = "okay";
        led0: led_0 {
            gpios = <&gpio0 26 GPIO_ACTIVE_LOW>; // red LED, connected to P0.26
        };
        led1: led_1 {
            gpios = <&gpio0 30 GPIO_ACTIVE_LOW>; // green LED, connected to P0.30
        };
        led2: led_2 {
            gpios = <&gpio0 6 GPIO_ACTIVE_LOW>;  // blue LED, connected to P0.06
        };
    };
};
```

(If the LEDs are wired between GPIO and GND instead, use `GPIO_ACTIVE_HIGH` flag.)

Finally, turn on the widget in the configuration:

```ini
CONFIG_RGBLED_WIDGET=y
```
