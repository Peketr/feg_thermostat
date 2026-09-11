#ifndef GUI_H
#define GUI_H

// Display
#include "mikroe_ssd1351.h"
#include "glib.h"

#include <stdbool.h>
#include <stdint.h>

typedef enum {
  SCREEN_HOME = 0,
  SCREEN_NETWORK,
  SCREEN_SENSORS,
  SCREEN_SETTINGS,
  SCREEN_DISPLAY,
  SCREEN_INFO,
  SCREEN_COUNT
} gui_screen_t;

// Regular settings screen rows, also used as the +/- adjust target.
typedef enum {
  SETTING_HYSTERESIS = 0,
  SETTING_MAX_VALVES,
  SETTING_SHOW_EXTRA_SENSOR,
  SETTINGS_COUNT
} gui_setting_t;

// Display settings screen rows, kept separate from regular settings.
typedef enum {
  DISPLAY_SETTING_BRIGHTNESS = 0,
  DISPLAY_SETTING_AUTO_DIM,
  DISPLAY_SETTING_AUTO_DIM_TIMEOUT,
  DISPLAY_SETTING_DIM_BRIGHTNESS,
  DISPLAY_SETTINGS_COUNT
} gui_display_setting_t;

// Snapshot of everything the screens can render, so gui.c needs no sensor or
// stack calls of its own.
typedef struct {
  int16_t target_temp;      // hundredths of a degree C
  int16_t control_temp;     // hundredths of a degree C, source depends on control_uses_ntc
  int16_t other_temp;     // hundredths of a degree C, source depends on control_uses_ntc
  int16_t si7021_temp;      // hundredths of a degree C
  uint32_t si7021_rh;       // thousandths of a percent, as returned by the driver
  int32_t ntc_temp;         // hundredths of a degree C, or NTC_TEMP_INVALID
  uint16_t ntc_counts;
  uint8_t open_valves;
  bool heating_enabled;
  bool control_uses_ntc;
  int16_t hysteresis;       // hundredths of a degree C
  uint8_t max_valves;
  uint8_t brightness;       // percent, scales every drawn colour
  bool show_extra_sensor;  // whether to show the extra sensor on the home screen
  bool auto_dim_enabled;
  uint16_t auto_dim_timeout_s;
  uint8_t dim_brightness;
  uint8_t settings_index;
  uint8_t display_settings_index;
  bool btnd_previous;
  uint32_t uptime_ms;
  bool network_up;
  uint16_t pan_id;
  uint8_t radio_channel;
  int8_t radio_tx_power;
  uint16_t node_id;
  uint8_t eui64[8];
  uint16_t parent_id;
  int8_t avg_parent_rssi;
} ui_state_t;

void gui_draw(glib_context_t *glib_context, gui_screen_t screen, const ui_state_t *state);

void display_logo();

sl_status_t oled_init(glib_context_t* glib_context);

#endif