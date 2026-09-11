
#include "app/framework/include/af.h"
#include "network-formation.h"
#include "platform-header.h"
#include "sl_button.h"
#include "sl_simple_button.h"
#include "sl_sleeptimer.h"
#include "sl_status.h"
#include "sl_zigbee_debug_print.h"
#include "zap-id.h"
#include "zap-type.h"
#include "zigbee_app_framework_event.h"
#include "network-steering.h"

#include "zigbee_helpers.h"
#include "gui.h"
#include "ntc.h"

// RHT Sensor
#include "sl_i2cspm_instances.h"
#include "sl_si70xx.h"
#include <stdbool.h>
#include <stdint.h>

// Buttons and Relays
#include "sl_simple_led_instances.h"
#include "sl_simple_button_instances.h"
#include "sl_sleeptimer_config.h"
#include "sl_token_manager_api.h"
#include "sl_token_manager_defines.h"

#include <string.h>

#define SLEEPTIMER_TICKS_PER_SECOND \
  (32768ULL / SL_SLEEPTIMER_FREQ_DIVIDER)

#define BTN_NONE 0
#define BTNA 1
#define BTNB 2
#define BTNC 3
#define BTND 4
#define BTNP 5
#define BTNM 6
#define BUTTON_DEBOUNCE_MS 30

// ISR-safe single-producer (ISR) / single-consumer (main loop) button event queue.
typedef struct {
  uint8_t id;
  bool long_press;
  bool very_long_press;
} button_event_t;

#define BUTTON_QUEUE_SIZE 16
static volatile button_event_t button_queue[BUTTON_QUEUE_SIZE];
static volatile uint8_t button_queue_head = 0; // written only by the ISR
static volatile uint8_t button_queue_tail = 0; // written only by app_process_action

// Called from the button ISR. Drops the event if the queue is full.
static void button_queue_push(uint8_t id, bool long_press, bool very_long_press){
  uint8_t next = (button_queue_head + 1) % BUTTON_QUEUE_SIZE;
  if (next == button_queue_tail) {
    return;
  }
  button_queue[button_queue_head].id = id;
  button_queue[button_queue_head].long_press = long_press;
  button_queue[button_queue_head].very_long_press = very_long_press;
  button_queue_head = next;
}

// Called from app_process_action. Returns false when the queue is empty.
static bool button_queue_pop(button_event_t *event){
  if (button_queue_tail == button_queue_head) {
    return false;
  }
  *event = button_queue[button_queue_tail];
  button_queue_tail = (button_queue_tail + 1) % BUTTON_QUEUE_SIZE;
  return true;
}

sl_zigbee_af_event_t thermostat_tick_event;
void thermostat_tick();

sl_zigbee_af_event_t ui_tick_event;
void ui_tick();

glib_context_t glib_context;

#define UI_TICK_PERIOD_MS 250
#define SELF_IDENTIFY_LENGTH_MS 2000
#define SCREEN_TIMEOUT_MS 30000
#define SCREEN_SHORT_TIMEOUT_MS 5000
#define HYSTERESIS_MIN 10
#define HYSTERESIS_MAX 200
#define HYSTERESIS_STEP 10
#define BRIGHTNESS_MIN 20
#define BRIGHTNESS_MAX 100
#define BRIGHTNESS_STEP 10
#define AUTO_DIM_TIMEOUT_MIN_S 5
#define AUTO_DIM_TIMEOUT_MAX_S 120
#define AUTO_DIM_TIMEOUT_STEP_S 5
#define DIM_BRIGHTNESS_MIN 5
#define DIM_BRIGHTNESS_MAX 50
#define DIM_BRIGHTNESS_STEP 5

static volatile gui_screen_t current_screen = SCREEN_HOME;
static uint32_t last_ui_activity_ms = 0;

// User settings.
static bool control_uses_ntc = false;
static uint8_t hysteresis_x100 = 50;
static uint8_t max_valves = 2;
static uint8_t brightness_pct = 100;
static bool show_extra_sensor = false;
static bool auto_dim_enabled = false;
static uint16_t auto_dim_timeout_s = 30;
static uint8_t dim_brightness_pct = 20;
static bool screen_dimmed = false;
static uint8_t settings_index = SETTING_HYSTERESIS;
static uint8_t display_settings_index = DISPLAY_SETTING_BRIGHTNESS;

static ui_state_t ui_state;

static void save_settings(uint8_t settings_mask);

static void save_settings(uint8_t settings_mask){
  thermostat_settings_token_t settings = {
    .control_uses_ntc = control_uses_ntc,
    .hysteresis_x100 = hysteresis_x100,
    .max_valves = max_valves,
    .brightness_pct = brightness_pct,
    .show_extra_sensor = show_extra_sensor,
    .auto_dim_enabled = auto_dim_enabled,
    .auto_dim_timeout_s = auto_dim_timeout_s,
    .dim_brightness_pct = dim_brightness_pct
  };

  const sl_status_t status = sl_token_manager_set_data(THERMOSTAT_SETTINGS_TOKEN,
                                                        &settings,
                                                        sizeof(settings));
  save_settings_to_attributes(&settings, settings_mask);
  if (status != SL_ZIGBEE_ZCL_STATUS_SUCCESS) {
    sl_zigbee_app_debug_println("settings save error: 0x%x", status);
  }


}

static void load_settings(void){
  thermostat_settings_token_t settings;
  /*sl_zigbee_af_status_t status = load_settings_from_attributes(&settings);*/
  const sl_status_t status = sl_token_manager_get_data(THERMOSTAT_SETTINGS_TOKEN,
                                                        &settings,
                                                        sizeof(settings));
  if (status != SL_ZIGBEE_ZCL_STATUS_SUCCESS
      || settings.control_uses_ntc > 1
      || settings.auto_dim_enabled > 1
      || settings.hysteresis_x100 < HYSTERESIS_MIN
      || settings.hysteresis_x100 > HYSTERESIS_MAX
      || settings.max_valves < 1
      || settings.max_valves > 2
      || settings.brightness_pct < BRIGHTNESS_MIN
      || settings.brightness_pct > BRIGHTNESS_MAX
      || settings.auto_dim_timeout_s < AUTO_DIM_TIMEOUT_MIN_S
      || settings.auto_dim_timeout_s > AUTO_DIM_TIMEOUT_MAX_S
      || settings.dim_brightness_pct < DIM_BRIGHTNESS_MIN
      || settings.dim_brightness_pct > DIM_BRIGHTNESS_MAX) {
        sl_zigbee_app_debug_println("settings load error: 0x%x", status);
    return;
  }

  control_uses_ntc = settings.control_uses_ntc;
  hysteresis_x100 = settings.hysteresis_x100;
  max_valves = settings.max_valves;
  brightness_pct = settings.brightness_pct;
  show_extra_sensor = settings.show_extra_sensor;
  auto_dim_enabled = settings.auto_dim_enabled;
  auto_dim_timeout_s = settings.auto_dim_timeout_s;
  dim_brightness_pct = settings.dim_brightness_pct;

  save_settings_to_attributes(&settings, 0xFF); // write all attributes to ensure they are in sync

}

static uint8_t fetch_btn_id(const sl_button_t *handle){
  if (handle == &sl_button_btna) {
    return BTNA;
  }
  if (handle == &sl_button_btnb) {
    return BTNB;
  }
  if (handle == &sl_button_btnc) {
    return BTNC;
  }
  if (handle == &sl_button_btnd) {
    return BTND;
  }
  if (handle == &sl_button_btnplus) {
    return BTNP;
  }
  if (handle == &sl_button_btnminus) {
    return BTNM;
  }
  return BTN_NONE;
}

static const char *button_name(const sl_button_t *handle){
  if (handle == &sl_button_btna) {
    return "A";
  }
  if (handle == &sl_button_btnb) {
    return "B";
  }
  if (handle == &sl_button_btnc) {
    return "C";
  }
  if (handle == &sl_button_btnd) {
    return "D";
  }
  if (handle == &sl_button_btnplus) {
    return "Plus";
  }
  if (handle == &sl_button_btnminus) {
    return "Minus";
  }
  return "?";
}

static uint32_t now_ms(void){
  return sl_sleeptimer_tick_to_ms(sl_sleeptimer_get_tick_count());
}

// Runs in ISR context: only queue/flag manipulation here, never call
// sl_zigbee_af_event_* functions from this callback.
void sl_button_on_change(const sl_button_t *handle){
  static bool press_started = false;
  static uint8_t btn = BTN_NONE;
  static uint32_t pressbegin = 0;
  static uint32_t last_edge_ms[BTNM + 1];
  static bool edge_seen[BTNM + 1];

  const bool is_pressed = handle->get_state(handle) == SL_SIMPLE_BUTTON_PRESSED;
  const uint8_t button_id = fetch_btn_id(handle);
  const uint32_t edge_ms = now_ms();

  if (button_id == BTN_NONE) {
    return;
  }

  if (edge_seen[button_id]
      && edge_ms - last_edge_ms[button_id] < BUTTON_DEBOUNCE_MS) {
    return;
  }
  edge_seen[button_id] = true;
  last_edge_ms[button_id] = edge_ms;

  if (!press_started && is_pressed) {
    press_started = true;
    btn = button_id;
    pressbegin = edge_ms;
  } else if (press_started && !is_pressed && button_id == btn) {
    const uint32_t held_ms = edge_ms - pressbegin;
    button_queue_push(btn, held_ms > 1000, held_ms > 10000);
    btn = BTN_NONE;
    press_started = false;
  }

  sl_zigbee_app_debug_println("Button %s %s", button_name(handle), is_pressed ? "Pressed" : "Released");
}


static uint8_t actuate_heating(int16_t current_temp, int16_t target_temp, bool enable){
  uint8_t valves_to_open = 0;

  if (enable) {
    if (current_temp < target_temp - hysteresis_x100) {
      valves_to_open = max_valves;
    } else if (current_temp < target_temp + hysteresis_x100) {
      valves_to_open = 1;
    }
  }

  sl_zigbee_app_debug_println("valves_to_open: %d", valves_to_open);

  switch (valves_to_open) {
    case 2:
      sl_led_turn_on(&sl_led_heat1);
      sl_led_turn_on(&sl_led_heat2);
      break;
    case 1:
      sl_led_turn_on(&sl_led_heat1);
      sl_led_turn_off(&sl_led_heat2);
      break;
    case 0:
    default:
      sl_led_turn_off(&sl_led_heat1);
      sl_led_turn_off(&sl_led_heat2);
      break;
  }

  return valves_to_open;
}



// #######################################################################
// MAIN OPERATION ########################################################
// #######################################################################

// Refreshes the fields that change faster than the 30s sensor tick, then draws.
static void render_current_screen(void){
  load_settings();
  ui_state.control_uses_ntc = control_uses_ntc;
  ui_state.hysteresis = hysteresis_x100;
  ui_state.max_valves = max_valves;
  ui_state.brightness = screen_dimmed ? dim_brightness_pct : brightness_pct;
  ui_state.show_extra_sensor = show_extra_sensor;
  ui_state.auto_dim_enabled = auto_dim_enabled;
  ui_state.auto_dim_timeout_s = auto_dim_timeout_s;
  ui_state.dim_brightness = dim_brightness_pct;
  ui_state.settings_index = settings_index;
  ui_state.display_settings_index = display_settings_index;
  ui_state.uptime_ms = now_ms();
  ui_state.network_up = on_network();

  if (ui_state.network_up) {
    sl_zigbee_node_type_t node_type;
    sl_zigbee_network_parameters_t params;
    if (sl_zigbee_get_network_parameters(&node_type, &params) == SL_STATUS_OK) {
      ui_state.pan_id = params.panId;
      ui_state.radio_channel = params.radioChannel;
      ui_state.radio_tx_power = params.radioTxPower;
      ui_state.parent_id = sl_zigbee_get_parent_id();
      ui_state.avg_parent_rssi = sl_zigbee_get_avg_parent_rssi();
    }
    ui_state.node_id = sl_zigbee_get_node_id();
  }

  const uint8_t *eui64 = sl_zigbee_get_eui64();
  if (eui64 != NULL) {
    memcpy(ui_state.eui64, eui64, sizeof(ui_state.eui64));
  }

  gui_draw(&glib_context, current_screen, &ui_state);
}

// Arms the fast UI tick; it keeps rescheduling itself while it is needed.
static void arm_ui_tick(void){
  sl_zigbee_af_event_set_active(&ui_tick_event);
}

static void note_ui_activity(void){
  last_ui_activity_ms = now_ms();
}

void thermostat_tick(void){
  sl_status_t sc;
  uint32_t rh_data;
  int32_t temp_data;
  int16_t target_temp;
  uint8_t system_mode;

  sc = sl_si70xx_measure_rh_and_temp(sl_i2cspm_inst0, SI7021_ADDR, &rh_data, &temp_data);
  if (sc) {
    sl_zigbee_app_debug_println("error: 0x%x", sc);
  } else {
    sl_zigbee_app_debug_println("periodic: %d,%d", rh_data / 1000, temp_data / 1000);
  }

  const uint16_t ntc_counts = ntc_read_counts();
  const int32_t ntc_temp = ntc_read_temp_c_x100();
  if (ntc_temp == NTC_TEMP_INVALID) {
    sl_zigbee_app_debug_println("ntc: open/short, counts %d", ntc_counts);
  } else {
    sl_zigbee_app_debug_println("ntc: counts %d, %d.%02d C", ntc_counts,
                                ntc_temp / 100,
                                (int)(ntc_temp < 0 ? -ntc_temp : ntc_temp) % 100);
  }

  const sl_zigbee_af_status_t status = get_target(&target_temp, &system_mode);
  if (status) {
    sl_zigbee_app_debug_println("Target read error: 0x%x", status);
  }
  sl_zigbee_app_debug_println("target temperature: %d, mode: 0x%x", target_temp / 100, system_mode);

  const int16_t si7021_temp = temp_data / 10;
  const bool source_valid = control_uses_ntc ? (ntc_temp != NTC_TEMP_INVALID) : (sc == SL_STATUS_OK);
  const int16_t current_temp = control_uses_ntc ? (int16_t)ntc_temp : si7021_temp;
  const bool enable_heating = status == SL_ZIGBEE_ZCL_STATUS_SUCCESS && source_valid && system_mode == 0x04;
  const uint8_t open_valves = actuate_heating(current_temp, target_temp, enable_heating);

  if (source_valid) {
    update_measurement(rh_data, 10 * current_temp);
  } else {
    sl_zigbee_app_debug_println("measurement update skipped due to invalid source");
  }

  sl_zigbee_app_debug_println("control source: %s, temp: %d", control_uses_ntc ? "NTC" : "SI7021", current_temp);

  update_running_state(open_valves);

  ui_state.target_temp = target_temp;
  ui_state.control_temp = current_temp;
  ui_state.si7021_temp = sc == SL_STATUS_OK ? si7021_temp : (int16_t)0;
  ui_state.si7021_rh = rh_data;
  ui_state.ntc_temp = ntc_temp;
  ui_state.other_temp = control_uses_ntc ? si7021_temp :(int16_t)ntc_temp;
  ui_state.ntc_counts = ntc_counts;
  ui_state.open_valves = open_valves;
  ui_state.heating_enabled = enable_heating;
  ui_state.show_extra_sensor = show_extra_sensor;

  render_current_screen();
  sl_zigbee_af_event_set_delay_ms(&thermostat_tick_event, 30000);
}

// Runs at a fixed rate while a non-home screen is showing, so those screens
// stay current and can time out.
void ui_tick(void){
  if (!screen_dimmed && auto_dim_enabled
      && now_ms() - last_ui_activity_ms >= (uint32_t)auto_dim_timeout_s * 1000u) {
    screen_dimmed = true;
  }

  if (current_screen != SCREEN_HOME
      && now_ms() - last_ui_activity_ms > SCREEN_TIMEOUT_MS) {
    current_screen = SCREEN_HOME;
  }

  const bool keep_running = current_screen != SCREEN_HOME
                            || (auto_dim_enabled && !screen_dimmed);

  render_current_screen();

  if (keep_running) {
    sl_zigbee_af_event_set_delay_ms(&ui_tick_event, UI_TICK_PERIOD_MS);
  }
}


// APPLICATION FRAMEWORK FUNCTIONS

void app_init(){
  sl_status_t sc;

  sc = sl_si70xx_init(sl_i2cspm_inst0, SI7021_ADDR);
  if (sc)
    sl_zigbee_app_debug_println(" init error: 0x%x",sc);

  ntc_init();

  load_settings();

  
  sl_zigbee_af_event_init(&thermostat_tick_event, thermostat_tick);
  sl_zigbee_af_event_init(&ui_tick_event, ui_tick);
  
  oled_init(&glib_context);
  last_ui_activity_ms = now_ms();
  if (auto_dim_enabled) {
    arm_ui_tick();
  }
  
  sl_zigbee_af_event_set_delay_ms(&thermostat_tick_event, 2000);

}

static void trigger_thermostat_tick(void){
  sl_zigbee_af_event_set_delay_ms(&thermostat_tick_event, 100);
}

static void cycle_screen(bool backwards){
  const gui_screen_t screen = current_screen;
  current_screen = backwards
                   ? (gui_screen_t)((screen + SCREEN_COUNT - 1) % SCREEN_COUNT)
                   : (gui_screen_t)((screen + 1) % SCREEN_COUNT);
  if (current_screen == SCREEN_DISPLAY) {
    if (display_settings_index >= DISPLAY_SETTINGS_COUNT) {
      display_settings_index = DISPLAY_SETTING_BRIGHTNESS;
    }
  } else if (current_screen == SCREEN_SETTINGS && settings_index >= SETTINGS_COUNT) {
    settings_index = SETTING_HYSTERESIS;
  }
  sl_zigbee_app_debug_println("screen: %d", current_screen);
}

static void adjust_setting(int8_t direction){
  if (current_screen == SCREEN_SETTINGS && settings_index == SETTING_HYSTERESIS) {
    int16_t value = hysteresis_x100 + direction * HYSTERESIS_STEP;
    if (value < HYSTERESIS_MIN) {
      value = HYSTERESIS_MIN;
    } else if (value > HYSTERESIS_MAX) {
      value = HYSTERESIS_MAX;
    }
    hysteresis_x100 = value;
  } else if (current_screen == SCREEN_DISPLAY && display_settings_index == DISPLAY_SETTING_BRIGHTNESS) {
    int16_t value = brightness_pct + direction * BRIGHTNESS_STEP;
    if (value < BRIGHTNESS_MIN) {
      value = BRIGHTNESS_MIN;
    } else if (value > BRIGHTNESS_MAX) {
      value = BRIGHTNESS_MAX;
    }
    brightness_pct = (uint8_t)value;
  } else if (current_screen == SCREEN_SETTINGS && settings_index == SETTING_SHOW_EXTRA_SENSOR) {
    show_extra_sensor = !show_extra_sensor;
  } else if (current_screen == SCREEN_DISPLAY && display_settings_index == DISPLAY_SETTING_AUTO_DIM) {
    auto_dim_enabled = !auto_dim_enabled;
    screen_dimmed = false;
  } else if (current_screen == SCREEN_DISPLAY && display_settings_index == DISPLAY_SETTING_AUTO_DIM_TIMEOUT) {
    int16_t value = auto_dim_timeout_s + direction * AUTO_DIM_TIMEOUT_STEP_S;
    if (value < AUTO_DIM_TIMEOUT_MIN_S) {
      value = AUTO_DIM_TIMEOUT_MIN_S;
    } else if (value > AUTO_DIM_TIMEOUT_MAX_S) {
      value = AUTO_DIM_TIMEOUT_MAX_S;
    }
    auto_dim_timeout_s = (uint16_t)value;
  } else if (current_screen == SCREEN_DISPLAY && display_settings_index == DISPLAY_SETTING_DIM_BRIGHTNESS) {
    int16_t value = dim_brightness_pct + direction * DIM_BRIGHTNESS_STEP;
    if (value < DIM_BRIGHTNESS_MIN) {
      value = DIM_BRIGHTNESS_MIN;
    } else if (value > DIM_BRIGHTNESS_MAX) {
      value = DIM_BRIGHTNESS_MAX;
    }
    dim_brightness_pct = (uint8_t)value;
  } else if (current_screen == SCREEN_SETTINGS && settings_index == SETTING_MAX_VALVES) {
    max_valves = direction > 0 ? 2 : 1;
  }

  uint8_t settings_mask = 0;
  if (current_screen == SCREEN_SETTINGS && settings_index == SETTING_HYSTERESIS) {
    settings_mask |= 0x02;
  } else if (current_screen == SCREEN_DISPLAY && display_settings_index == DISPLAY_SETTING_BRIGHTNESS) {
    settings_mask |= 0x08;
  } else if (current_screen == SCREEN_SETTINGS && settings_index == SETTING_SHOW_EXTRA_SENSOR) {
    settings_mask |= 0x10;
  } else if (current_screen == SCREEN_SETTINGS && settings_index == SETTING_MAX_VALVES) {
    settings_mask |= 0x04;
  } else if (current_screen == SCREEN_DISPLAY && display_settings_index == DISPLAY_SETTING_AUTO_DIM) {
    settings_mask |= 0x20;
  } else if (current_screen == SCREEN_DISPLAY && display_settings_index == DISPLAY_SETTING_AUTO_DIM_TIMEOUT) {
    settings_mask |= 0x40;
  } else if (current_screen == SCREEN_DISPLAY && display_settings_index == DISPLAY_SETTING_DIM_BRIGHTNESS) {
    settings_mask |= 0x80;
  }
  save_settings(settings_mask);
}

// Buttons other than D are remapped per screen; see the screen hints in gui.c.
static void handle_screen_button(const button_event_t *event){
  int16_t target_temp;
  uint8_t heating_enabled;
  const sl_zigbee_af_status_t status = get_target(&target_temp, &heating_enabled);

  switch (current_screen) {
    case SCREEN_NETWORK:
      if (event->id == BTNA && !on_network()) {
        sl_zigbee_af_network_steering_start();
      } else if (event->id == BTNB && on_network()) {
        sl_zigbee_leave_network(SL_ZIGBEE_LEAVE_NWK_WITH_NO_OPTION);
      } else if (event->id == BTNC) {
        display_logo();
        sl_zigbee_af_event_set_delay_ms(&ui_tick_event, SELF_IDENTIFY_LENGTH_MS);
      }
      break;

    case SCREEN_SENSORS:
      if (event->id == BTNA) {
        control_uses_ntc = false;
      } else if (event->id == BTNB) {
        control_uses_ntc = true;
      } else if (event->id == BTNP || event->id == BTNM) {
        control_uses_ntc = !control_uses_ntc;
      }
      save_settings(0x01);
      trigger_thermostat_tick();
      break;

    case SCREEN_SETTINGS:
      if (event->id == BTNA) {
        settings_index = (settings_index + SETTINGS_COUNT - 1) % SETTINGS_COUNT;
      } else if (event->id == BTNB) {
        settings_index = (settings_index + 1) % SETTINGS_COUNT;
      } else if (event->id == BTNP) {
        adjust_setting(1);
        trigger_thermostat_tick();
      } else if (event->id == BTNM) {
        adjust_setting(-1);
        trigger_thermostat_tick();
      }
      break;

    case SCREEN_DISPLAY:
      if (event->id == BTNA) {
        display_settings_index = (display_settings_index + DISPLAY_SETTINGS_COUNT - 1)
                                  % DISPLAY_SETTINGS_COUNT;
      } else if (event->id == BTNB) {
        display_settings_index = (display_settings_index + 1) % DISPLAY_SETTINGS_COUNT;
      } else if (event->id == BTNP) {
        adjust_setting(1);
        trigger_thermostat_tick();
      } else if (event->id == BTNM) {
        adjust_setting(-1);
        trigger_thermostat_tick();
      }
      break;

    case SCREEN_INFO:
      break;

    case SCREEN_HOME:
    default:
      switch (event->id) {
        case BTNP:
          if (status == SL_ZIGBEE_ZCL_STATUS_SUCCESS && heating_enabled) {
            set_target_temp(target_temp + 50);
          }
          break;
        case BTNM:
          if (status == SL_ZIGBEE_ZCL_STATUS_SUCCESS && heating_enabled) {
            set_target_temp(target_temp - 50);
          }
          break;
        case BTNA:
          if (status == SL_ZIGBEE_ZCL_STATUS_SUCCESS) {
            set_system_mode(true);
          }
          break;
        case BTNB:
          if (status == SL_ZIGBEE_ZCL_STATUS_SUCCESS) {
            set_system_mode(false);
          }
          break;
        case BTNC:
          if (!on_network()) {
            sl_zigbee_af_network_steering_start();
          }
          break;
        default:
          break;
      }
      trigger_thermostat_tick();
      break;
  }
}

static void handle_button_event(const button_event_t *event){
  if (screen_dimmed && event->id != BTN_NONE) {
    sl_zigbee_app_debug_println("undimming screen");
    screen_dimmed = false;
  } else if (!auto_dim_enabled && current_screen == SCREEN_HOME && event->id == BTNC && on_network()) {
    sl_zigbee_app_debug_println("dimming screen");
    screen_dimmed = true;
  }

  if (event->id != BTN_NONE) { // source wasn't a button press, so don't reset the timeout
    note_ui_activity(); 
    screen_dimmed = false;
  }

  if (event->id == BTND) {
    cycle_screen(event->long_press);
  } else {
    handle_screen_button(event);
  }

  if (current_screen != SCREEN_HOME || auto_dim_enabled) {
    arm_ui_tick();
  } else {
    trigger_thermostat_tick();
  }
}

void app_process_action(void)
{
  button_event_t event;
  while (button_queue_pop(&event)) {
    load_settings();
    handle_button_event(&event);
  }
}

void sl_zigbee_af_post_attribute_change_cb(int8u endpoint,
                                                sl_zigbee_af_cluster_id_t clusterId,
                                                sl_zigbee_af_attribute_id_t attributeId,
                                                int8u mask,
                                                int16u manufacturerCode,
                                                int8u type,
                                                int8u size,
                                                int8u* value)
{
  UNUSED_VAR(mask);
  UNUSED_VAR(manufacturerCode);
  UNUSED_VAR(type);
  UNUSED_VAR(size);

  sl_zigbee_app_debug_println("attribute change: endpoint %d, cluster 0x%04X, attribute 0x%04X, mask 0x%02X", endpoint, clusterId, attributeId, mask);

  if (endpoint == THERMOSTAT_ENDPOINT && clusterId == ZCL_THERMOSTAT_SETTINGS_CLUSTER_ID) {
    switch (attributeId) {
      case ZCL_THERMOSTAT_SETTINGS_CONTROL_USES_NTC_ATTRIBUTE_ID:
        if ((bool) *value == control_uses_ntc) {
          return;
        } else {
          control_uses_ntc = (bool) *value;
          current_screen = SCREEN_SENSORS; // force the user to see the change
        }
        break;
      case ZCL_THERMOSTAT_SETTINGS_HYSTERESIS_X100_ATTRIBUTE_ID:
        if (*value == hysteresis_x100) {
          return;
        } else {
          hysteresis_x100 = *value;
          current_screen = SCREEN_SETTINGS; // force the user to see the change
          settings_index = SETTING_HYSTERESIS; // force the user to see the change
        }
        break;
      case ZCL_THERMOSTAT_SETTINGS_MAX_VALVES_ATTRIBUTE_ID:
        if (*value == max_valves) {
          return;
        } else {
          max_valves = *value;
          current_screen = SCREEN_SETTINGS; // force the user to see the change
          settings_index = SETTING_MAX_VALVES; // force the user to see the change
        }
        break;
      case ZCL_THERMOSTAT_SETTINGS_BRIGHTNESS_PCT_ATTRIBUTE_ID:
        if (*value == brightness_pct) {
          return;
        } else {
          brightness_pct = *value;
          current_screen = SCREEN_DISPLAY; // force the user to see the change
          display_settings_index = DISPLAY_SETTING_BRIGHTNESS; // force the user to see the change
        }
        break;
      case ZCL_THERMOSTAT_SETTINGS_SHOW_EXTRA_SENSOR_ATTRIBUTE_ID:
        if ((bool) *value == show_extra_sensor) {
          return;
        } else {
          show_extra_sensor = (bool) *value;
          current_screen = SCREEN_SETTINGS; // force the user to see the change
          settings_index = SETTING_SHOW_EXTRA_SENSOR; // force the user to see the change
        }
        break;
      case ZCL_THERMOSTAT_SETTINGS_AUTO_DIM_ENABLED_ATTRIBUTE_ID:
        if ((bool) *value == auto_dim_enabled) {
          return;
        } else {
          auto_dim_enabled = (bool) *value;
          screen_dimmed = false;
          current_screen = SCREEN_DISPLAY; // force the user to see the change
          display_settings_index = DISPLAY_SETTING_AUTO_DIM; // force the user to see the change
        }
        break;
      case ZCL_THERMOSTAT_SETTINGS_AUTO_DIM_TIMEOUT_S_ATTRIBUTE_ID:
        if (*(uint16_t *)value == auto_dim_timeout_s) {
          return;
        } else {
          memcpy(&auto_dim_timeout_s, value, sizeof(auto_dim_timeout_s));
          current_screen = SCREEN_DISPLAY; // force the user to see the change
          display_settings_index = DISPLAY_SETTING_AUTO_DIM_TIMEOUT; // force the user to see the change
        }
        break;
      case ZCL_THERMOSTAT_SETTINGS_DIM_BRIGHTNESS_PCT_ATTRIBUTE_ID:
        if (*value == dim_brightness_pct) {
          return;
        } else {
          dim_brightness_pct = *value;
          current_screen = SCREEN_DISPLAY; // force the user to see the change
          display_settings_index = DISPLAY_SETTING_DIM_BRIGHTNESS; // force the user to see the change
        }
        break;
      default:
        return;
    }
    last_ui_activity_ms = now_ms() - SCREEN_TIMEOUT_MS + SCREEN_SHORT_TIMEOUT_MS; 

    sl_zigbee_app_debug_println("thermostat settings changed, reloading");
    save_settings(0xFF);
    button_queue_push(BTN_NONE, false, false);
  }

  if (endpoint == THERMOSTAT_ENDPOINT && clusterId == ZCL_THERMOSTAT_CLUSTER_ID && (attributeId== ZCL_SYSTEM_MODE_ATTRIBUTE_ID || attributeId == ZCL_OCCUPIED_HEATING_SETPOINT_ATTRIBUTE_ID)){
    //Trick to trigger a thermostat tick to update the display and actuate heating.
    button_queue_push(BTN_NONE, false, false);
  }
}

bool sl_zigbee_af_pre_command_received_cb(sl_zigbee_af_cluster_command_t* cmd)
{
  if (cmd->apsFrame->clusterId == ZCL_THERMOSTAT_SETTINGS_CLUSTER_ID && cmd->commandId == 0x00) {
    sl_zigbee_app_debug_println("Received Dim Command");
    screen_dimmed = true;
    button_queue_push(BTN_NONE, false, false); // trigger a thermostat tick to update the display
    return true; 
  }
  return false;
}

/** @brief Complete network steering.
 *
 * This callback is fired when the Network Steering plugin is complete.
 *
 * @param status On success this will be set to SL_STATUS_OK to indicate a
 * network was joined successfully. On failure this will be the status code of
 * the last join or scan attempt. Ver.: always
 *
 * @param totalBeacons The total number of 802.15.4 beacons that were heard,
 * including beacons from different devices with the same PAN ID. Ver.: always
 * @param joinAttempts The number of join attempts that were made to get onto
 * an open Zigbee network. Ver.: always
 *
 * @param finalState The finishing state of the network steering process. From
 * this, one is able to tell on which channel mask and with which key the
 * process was complete. Ver.: always
 */
void sl_zigbee_af_network_steering_complete_cb(sl_status_t status,
                                               uint8_t totalBeacons,
                                               uint8_t joinAttempts,
                                               uint8_t finalState)
{
  UNUSED_VAR(totalBeacons);
  UNUSED_VAR(joinAttempts);
  UNUSED_VAR(finalState);
  sl_zigbee_app_debug_println("%s network %s: 0x%02X", "Join", "complete", status);
  trigger_thermostat_tick();
}

/** @brief
 *
 * Application framework equivalent of ::sl_zigbee_radio_needs_calibrating_handler
 */
void sl_zigbee_af_radio_needs_calibrating_cb(void)
{
  sl_mac_calibrate_current_channel();
}

void sli_zigbee_af_stack_status_callback(sl_status_t status){
  //Trick to trigger a thermostat tick to update the display and actuate heating.
  button_queue_push(BTN_NONE, false, false);
  switch (status) {
    case SL_STATUS_NETWORK_UP:
    case SL_STATUS_ZIGBEE_TRUST_CENTER_SWAP_EUI_HAS_CHANGED:      // also means NETWORK_UP
    case SL_STATUS_ZIGBEE_TRUST_CENTER_SWAP_EUI_HAS_NOT_CHANGED:  // also means NETWORK_UP
    {
      sl_zigbee_af_app_println("SL_STATUS_NETWORK_UP 0x%04X", sl_zigbee_af_get_node_id());
      sl_zigbee_af_app_flush();

      if (status == SL_STATUS_NETWORK_UP) {
        sl_zigbee_start_writing_stack_tokens();
      } else {
        sl_zigbee_af_app_println("Trust Center EUI has %schanged.",
                                 (status == SL_STATUS_ZIGBEE_TRUST_CENTER_SWAP_EUI_HAS_CHANGED) ? "" : "not ");
        sl_zigbee_af_registration_abort_cb();
        sl_zigbee_af_registration_start_cb();
      }
      sl_zigbee_af_registration_start_cb();
      break;
    }

    case SL_STATUS_ZIGBEE_RECEIVED_KEY_IN_THE_CLEAR:
    case SL_STATUS_ZIGBEE_NO_NETWORK_KEY_RECEIVED:
    case SL_STATUS_ZIGBEE_NO_LINK_KEY_RECEIVED:
    case SL_STATUS_ZIGBEE_PRECONFIGURED_KEY_REQUIRED:
    case SL_STATUS_ZIGBEE_MOVE_FAILED:
    case SL_STATUS_NOT_JOINED:
    case SL_STATUS_NO_BEACONS:
    case SL_STATUS_NETWORK_DOWN:
      if (status == SL_STATUS_NETWORK_DOWN) {
        sl_zigbee_af_app_println("SL_STATUS_NETWORK_DOWN");
      } else {
        sl_zigbee_af_app_println("SL_STATUS_NOT_JOINED");
      }
      sl_zigbee_af_app_flush();
      sl_zigbee_af_stack_down();
      break;
    case SL_STATUS_ZIGBEE_NETWORK_OPENED:
      sl_zigbee_af_app_println("SL_STATUS_ZIGBEE_NETWORK_OPENED: %d sec", sl_zigbee_af_get_open_network_duration_sec());
      return;

    case SL_STATUS_ZIGBEE_NETWORK_CLOSED:
      sl_zigbee_af_app_println("SL_STATUS_ZIGBEE_NETWORK_CLOSED");
      return;

    case SL_STATUS_ZIGBEE_REJOIN_FAILED_BUT_NETWORK_RESTORED:
      sl_zigbee_af_app_println("SL_STATUS_ZIGBEE_REJOIN_FAILED_BUT_NETWORK_RESTORED");
      return;

    default:
      sl_zigbee_af_debug_println("EVENT: stackStatus 0x%08X", status);
  }
}

void sl_zigbee_af_identify_start_feedback_cb(uint8_t endpoint, uint16_t identifyTime){
  UNUSED_VAR(endpoint);
  UNUSED_VAR(identifyTime);
  
  display_logo();
}