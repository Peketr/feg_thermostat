
#include "gui.h"

#include "glib.h"
#include "glib_font.h"
#include "micro-common.h"
#include "sl_device_peripheral.h"
#include "sl_spidrv_instances.h"

#include "zigbee_helpers.h"
#include "start_image.h"
#include "sl_sleeptimer.h"
#include "ntc.h"

#include <stdio.h>
#include <string.h>


#define COLOR_WHITE  0xFFFF
#define COLOR_BLACK  0x0000
#define COLOR_BLUE   0x001f
#define COLOR_ORANGE 0xfce0
#define COLOR_RED    0xf800
#define COLOR_GREEN  0x07e0
#define COLOR_GREY   0x8410

// 6x8 base font: 21 columns at text size 1.
#define LINE_H 10
#define TEXT_COLS 21
#define SCREEN_W 128
#define SCREEN_H 128
#define CHAR_W 6
#define CHAR_H 8

static uint8_t brightness_pct = 100;

// Scales each RGB565 channel so the brightness setting dims every element.
static uint16_t dim(uint16_t color){
  if (brightness_pct >= 100) {
    return color;
  }
  const uint16_t r = (uint16_t)(((color >> 11) & 0x1F) * brightness_pct / 100);
  const uint16_t g = (uint16_t)(((color >> 5) & 0x3F) * brightness_pct / 100);
  const uint16_t b = (uint16_t)((color & 0x1F) * brightness_pct / 100);
  return (uint16_t)((r << 11) | (g << 5) | b);
}

static void set_color(glib_context_t *ctx, uint16_t color){
  glib_set_text_color(ctx, dim(color));
}


sl_status_t oled_init(glib_context_t* glib_context){
  //  OLED initialization.
  sl_status_t sc = mikroe_ssd1351_init(sl_spidrv_inst0_handle);
  if (sc == SL_STATUS_OK) {
    glib_init(glib_context);
    glib_set_bg_color(glib_context, 0x0000);
    glib_set_text_color(glib_context, 0xFFFF);
    glib_enable_display(true);
    
    mikroe_ssd1351_image(ppcat128x128, 0, 0);
    //glib_update_display();  
    
  }
  return sc;
}

void display_logo(){
  mikroe_ssd1351_image(ppcat128x128, 0, 0);
}

static void draw_line(glib_context_t *ctx, uint8_t row, const char *str){
  glib_draw_string(ctx, str, 0, 12 + row * LINE_H);
}

// Grey hint lines stacked upward from the bottom edge.
static void draw_hints(glib_context_t *ctx, const char *const *lines, uint8_t count){
  set_color(ctx, COLOR_GREY);
  for (uint8_t i = 0; i < count; i++) {
    glib_draw_string(ctx, lines[i], 0, SCREEN_H - CHAR_H - (count - 1 - i) * LINE_H);
  }
  set_color(ctx, COLOR_WHITE);
}

// Right-edge labels whose y positions line up with the physical buttons.
static void draw_button_hints(glib_context_t *ctx, gui_screen_t screen){
  static const int16_t hint_y[] = { 0, 35, 85, 120 };
  const char* labels[SCREEN_COUNT][4] = {
    { "ON>" , "OFF>", on_network() ? "Dim>" : "Join>", "Next>"}, //HOME
    { "Join>", "Leave>", "Identify>", "Next>"}, //NETWORK
    { "A>", "B>", "C>", "Next>"}, //SENSORS
    { "Up>", "Down>", "", "Next>"}, //SETTINGS
    { "", "", "", "Next>"}, //INFO
  };

  set_color(ctx, COLOR_GREY);
  for (uint8_t i = 0; i < sizeof(hint_y) / sizeof(hint_y[0]); i++) {
    glib_draw_string(ctx, labels[screen][i], SCREEN_W - CHAR_W * (int16_t)strlen(labels[screen][i]), hint_y[i]);
  }
  set_color(ctx, COLOR_WHITE);
}

// Title bar plus the shared right-edge button hints.
static void draw_header(glib_context_t *ctx, const char *title, gui_screen_t screen){
  set_color(ctx, COLOR_ORANGE);
  glib_draw_string(ctx, title, 0, 0);
  set_color(ctx, COLOR_WHITE);
  draw_button_hints(ctx, screen);
}

static void draw_big_temp(glib_context_t *ctx, int16_t x, int16_t y, int16_t temp, uint8_t scale){
  const int16_t w = 6 * scale;
  const uint16_t fg = dim(COLOR_WHITE);
  char buf[TEXT_COLS + 1];
  
  glib_set_text_size(ctx, scale, scale);
  glib_set_text_color(ctx, fg);
  snprintf(buf, sizeof(buf), "%ld.%01ld", (long)(temp / 100), (long)(temp/10 % 10));
  glib_draw_string(ctx, buf, x, y);
  
  glib_set_text_size(ctx, 1, 1);  
}

static void format_temp_x100(char *buf, size_t len, int32_t temp_x100){
  if (temp_x100 == NTC_TEMP_INVALID) {
    snprintf(buf, len, "--.--");
    return;
  }
  const int32_t abs_temp = temp_x100 < 0 ? -temp_x100 : temp_x100;
  snprintf(buf, len, "%s%ld.%02ld", temp_x100 < 0 ? "-" : "",
           (long)(abs_temp / 100), (long)(abs_temp % 100));
}


// SCREENS

static void draw_home_screen(glib_context_t *glib_context, const ui_state_t *s){
  const int16_t target_temp = s->target_temp;
  const int16_t current_temp = s->control_temp;
  const int16_t other_temp = (int16_t)s->other_temp;

  draw_button_hints(glib_context, SCREEN_HOME);

  if (s->heating_enabled) {
    draw_big_temp(glib_context, 5, 20, target_temp, 4);
  }

  draw_big_temp(glib_context, 5, 60, current_temp, 4);
  draw_big_temp(glib_context, 5, 100, other_temp, 2);

  if (!s->heating_enabled){
    glib_draw_string(glib_context, "Off", 0, 0);
  } else if (s->open_valves == 0) {
    set_color(glib_context, COLOR_BLUE);
    glib_draw_string(glib_context, "Idle", 0, 0);
  } else if (s->open_valves == 1){
    set_color(glib_context, COLOR_ORANGE);
    glib_draw_string(glib_context, "Half Heat", 0, 0);
  } else if (s->open_valves == 2){
    set_color(glib_context, COLOR_RED);
    glib_draw_string(glib_context, "Heat", 0, 0);
  }
  set_color(glib_context, COLOR_WHITE);

  if (steering_in_progress()) {
    set_color(glib_context, COLOR_ORANGE);
    glib_draw_string(glib_context, "Connecting", 0, 120);
  } else if (on_network()){
    set_color(glib_context, COLOR_GREEN);
    glib_draw_string(glib_context, "Connected", 0, 120);
  } else {
    set_color(glib_context, COLOR_WHITE);
    glib_draw_string(glib_context, "Disconnected", 0, 120);
  }
  set_color(glib_context, COLOR_WHITE);
}

static void draw_network_screen(glib_context_t *ctx, const ui_state_t *s){
  char buf[TEXT_COLS + 1];

  draw_header(ctx, "NETWORK", SCREEN_NETWORK);

  if (steering_in_progress()) {
    switch (steering_state()) {
      case SL_ZIGBEE_AF_PLUGIN_NETWORK_STEERING_STATE_SCAN_PRIMARY_CONFIGURED:
      case SL_ZIGBEE_AF_PLUGIN_NETWORK_STEERING_STATE_SCAN_SECONDARY_CONFIGURED:
      case SL_ZIGBEE_AF_PLUGIN_NETWORK_STEERING_STATE_SCAN_PRIMARY_INSTALL_CODE:
      case SL_ZIGBEE_AF_PLUGIN_NETWORK_STEERING_STATE_SCAN_SECONDARY_INSTALL_CODE:
      case SL_ZIGBEE_AF_PLUGIN_NETWORK_STEERING_STATE_SCAN_PRIMARY_CENTRALIZED:
      case SL_ZIGBEE_AF_PLUGIN_NETWORK_STEERING_STATE_SCAN_SECONDARY_CENTRALIZED:
      case SL_ZIGBEE_AF_PLUGIN_NETWORK_STEERING_STATE_SCAN_PRIMARY_DISTRIBUTED:
      case SL_ZIGBEE_AF_PLUGIN_NETWORK_STEERING_STATE_SCAN_SECONDARY_DISTRIBUTED:
        set_color(ctx, COLOR_ORANGE);
        draw_line(ctx, 0, "Scanning...");
        set_color(ctx, COLOR_WHITE);
        break;
      case SL_ZIGBEE_AF_PLUGIN_NETWORK_STEERING_STATE_SCAN_FINISHED:
        set_color(ctx, COLOR_ORANGE);
        draw_line(ctx, 0, "Joining in progress...");
        set_color(ctx, COLOR_WHITE);
        break;
      default:
        if (steering_state() >= SL_ZIGBEE_AF_PLUGIN_NETWORK_STEERING_STATE_UPDATE_TCLK) {
          set_color(ctx, COLOR_ORANGE);
          draw_line(ctx, 0, "TCLK Update");
          set_color(ctx, COLOR_WHITE);
        }

    }
  } else if (s->network_up) {
    set_color(ctx, COLOR_GREEN);
    draw_line(ctx, 0, "Joined");
    set_color(ctx, COLOR_WHITE);
  } else {
    draw_line(ctx, 0, "Not joined");
  }

  if (s->network_up) {
    snprintf(buf, sizeof(buf), "PAN    0x%04X", s->pan_id);
    draw_line(ctx, 1, buf);
    snprintf(buf, sizeof(buf), "Chan   %u", s->radio_channel);
    draw_line(ctx, 2, buf);
    snprintf(buf, sizeof(buf), "Node   0x%04X", s->node_id);
    draw_line(ctx, 3, buf);
    snprintf(buf, sizeof(buf), "Power  %d dBm", s->radio_tx_power);
    draw_line(ctx, 4, buf);
    snprintf(buf, sizeof(buf), "Parent 0x%04X", s->parent_id);
    draw_line(ctx, 5, buf);
  }

  //static const char *const hints[] = { "A: join", "B: leave", "C: identify" };
  //draw_hints(ctx, hints, 3);
}

// '>' marks the sensor currently driving the heating control loop.
static void draw_sensors_screen(glib_context_t *ctx, const ui_state_t *s){
  char buf[TEXT_COLS + 1];
  char temp[12];

  draw_header(ctx, "SENSORS", SCREEN_SENSORS);

  glib_set_text_color(ctx, dim(s->control_uses_ntc ? COLOR_WHITE : COLOR_GREEN));
  format_temp_x100(temp, sizeof(temp), s->si7021_temp);
  snprintf(buf, sizeof(buf), "%c SI7021 %s C", s->control_uses_ntc ? ' ' : '>', temp);
  draw_line(ctx, 0, buf);
  snprintf(buf, sizeof(buf), "         %lu.%01lu %%RH",
           (unsigned long)(s->si7021_rh / 1000), (unsigned long)(s->si7021_rh / 100 % 10));
  draw_line(ctx, 1, buf);

  glib_set_text_color(ctx, dim(s->control_uses_ntc ? COLOR_GREEN : COLOR_WHITE));
  format_temp_x100(temp, sizeof(temp), s->ntc_temp);
  snprintf(buf, sizeof(buf), "%c NTC    %s C", s->control_uses_ntc ? '>' : ' ', temp);
  draw_line(ctx, 3, buf);
  snprintf(buf, sizeof(buf), "         %u counts", s->ntc_counts);
  draw_line(ctx, 4, buf);

  static const char *const hints[] = { "A: use SI7021", "B: use NTC", "+/-: swap", "C: re-read now" };
  draw_hints(ctx, hints, 4);
}

static void draw_settings_screen(glib_context_t *ctx, const ui_state_t *s){
  char buf[TEXT_COLS + 1];
  char value[12];

  draw_header(ctx, "SETTINGS", SCREEN_SETTINGS);

  format_temp_x100(value, sizeof(value), s->hysteresis);
  glib_set_text_color(ctx, dim(s->settings_index == SETTING_HYSTERESIS ? COLOR_GREEN : COLOR_WHITE));
  snprintf(buf, sizeof(buf), "%c Hyst   %s C", s->settings_index == SETTING_HYSTERESIS ? '>' : ' ', value);
  draw_line(ctx, 1, buf);

  glib_set_text_color(ctx, dim(s->settings_index == SETTING_MAX_VALVES ? COLOR_GREEN : COLOR_WHITE));
  snprintf(buf, sizeof(buf), "%c Valves %u", s->settings_index == SETTING_MAX_VALVES ? '>' : ' ', s->max_valves);
  draw_line(ctx, 3, buf);

  glib_set_text_color(ctx, dim(s->settings_index == SETTING_BRIGHTNESS ? COLOR_GREEN : COLOR_WHITE));
  snprintf(buf, sizeof(buf), "%c Bright %u %%", s->settings_index == SETTING_BRIGHTNESS ? '>' : ' ', s->brightness);
  draw_line(ctx, 5, buf);

  static const char *const hints[] = { "+/-: adjust" };
  draw_hints(ctx, hints, 1);
}

static void draw_info_screen(glib_context_t *ctx, const ui_state_t *s){
  char buf[TEXT_COLS + 1];

  draw_header(ctx, "DEVICE INFO", SCREEN_INFO);

  draw_line(ctx, 0, "EUI64");
  snprintf(buf, sizeof(buf), "%02X%02X%02X%02X%02X%02X%02X%02X",
           s->eui64[7], s->eui64[6], s->eui64[5], s->eui64[4],
           s->eui64[3], s->eui64[2], s->eui64[1], s->eui64[0]);
  draw_line(ctx, 1, buf);

  const uint32_t uptime_s = s->uptime_ms / 1000;
  draw_line(ctx, 3, "Uptime");
  snprintf(buf, sizeof(buf), "%lu:%02lu:%02lu",
           (unsigned long)(uptime_s / 3600),
           (unsigned long)(uptime_s / 60 % 60),
           (unsigned long)(uptime_s % 60));
  draw_line(ctx, 4, buf);

  static const char *const hints[] = {"Build date:", __DATE__, __TIME__ };
  draw_hints(ctx, hints, 3);
}

void gui_draw(glib_context_t *glib_context, gui_screen_t screen, const ui_state_t *state){
  brightness_pct = state->brightness;

  glib_clear(glib_context);
  set_color(glib_context, COLOR_WHITE);

  switch (screen) {
    case SCREEN_NETWORK:
      draw_network_screen(glib_context, state);
      break;
    case SCREEN_SENSORS:
      draw_sensors_screen(glib_context, state);
      break;
    case SCREEN_SETTINGS:
      draw_settings_screen(glib_context, state);
      break;
    case SCREEN_INFO:
      draw_info_screen(glib_context, state);
      break;
    case SCREEN_HOME:
    default:
      draw_home_screen(glib_context, state);
      break;
  }

  glib_set_text_color(glib_context, COLOR_WHITE);
  glib_update_display();
}
