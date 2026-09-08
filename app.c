
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

#define SLEEPTIMER_TICKS_PER_SECOND \
  (32768ULL / SL_SLEEPTIMER_FREQ_DIVIDER)

#define BTN_NONE 0
#define BTNA 1
#define BTNB 2
#define BTNC 3
#define BTND 4
#define BTNP 5
#define BTNM 6

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

sl_zigbee_af_event_t decommission_watch_event;
void decommission_watch_tick();

volatile bool retrigger = false;
// Set/cleared from the button ISR; only read (never armed from the ISR) elsewhere.
volatile bool decommission_started = false;
volatile uint32_t decommission_start_time = 0;

glib_context_t glib_context;

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

  const bool is_pressed = handle->get_state(handle) == SL_SIMPLE_BUTTON_PRESSED;
  const uint8_t button_id = fetch_btn_id(handle);

  if (!press_started && is_pressed) {
    press_started = true;
    btn = button_id;
    pressbegin = now_ms();
  } else if (press_started && !is_pressed && button_id == btn) {
    const uint32_t held_ms = now_ms() - pressbegin;
    button_queue_push(btn, held_ms > 1000, held_ms > 10000);
    btn = BTN_NONE;
    press_started = false;
  }

  if (handle == &sl_button_btnc) {
    decommission_started = is_pressed && on_network();
    if (decommission_started) {
      decommission_start_time = now_ms();
    }
  }

  sl_zigbee_app_debug_println("Button %s %s", button_name(handle), is_pressed ? "Pressed" : "Released");
}


static uint8_t actuate_heating(int16_t current_temp, int16_t target_temp, bool enable){
  uint8_t valves_to_open = 0;

  if (enable) {
    if (current_temp < target_temp - 50) {
      valves_to_open = 2;
    } else if (current_temp < target_temp + 50) {
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

// Cached so the decommission-hold watchdog can redraw without recomputing state.
static int16_t last_target_temp = 0;
static int16_t last_current_temp = 0;
static int32_t last_ntc_temp = 0;
static uint8_t last_open_valves = 0;
static bool last_heating_enabled = false;

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
    update_measurement(rh_data, temp_data);
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

  const int16_t current_temp = temp_data / 10;
  const bool enable_heating = status == SL_ZIGBEE_ZCL_STATUS_SUCCESS && sc == SL_STATUS_OK && system_mode == 0x04;
  const uint8_t open_valves = actuate_heating(current_temp, target_temp, enable_heating);

  update_running_state(open_valves);

  last_target_temp = target_temp;
  last_current_temp = current_temp;
  last_ntc_temp = ntc_temp;
  last_open_valves = open_valves;
  last_heating_enabled = enable_heating;

  draw_display(&glib_context, target_temp, current_temp, ntc_temp, open_valves, enable_heating);
  sl_zigbee_af_event_set_delay_ms(&thermostat_tick_event, 30000);
}

// Reschedules itself at a fixed rate only while BTNC is held, so the
// "hold to decommission" warning stays live without redrawing on every tick.
void decommission_watch_tick(){
  if (decommission_started) {
    draw_display(&glib_context, last_target_temp, last_current_temp, last_ntc_temp, last_open_valves, last_heating_enabled);
    sl_zigbee_af_event_set_delay_ms(&decommission_watch_event, 250);
  }
}


// APPLICATION FRAMEWORK FUNCTIONS

void app_init(){
  sl_status_t sc;

  sc = sl_si70xx_init(sl_i2cspm_inst0, SI7021_ADDR);
  if (sc)
    sl_zigbee_app_debug_println(" init error: 0x%x",sc);

  ntc_init();

  
  sl_zigbee_af_event_init(&thermostat_tick_event, thermostat_tick);
  sl_zigbee_af_event_init(&decommission_watch_event, decommission_watch_tick);
  
  oled_init(&glib_context);
  retrigger = false;
  
  sl_zigbee_af_event_set_delay_ms(&thermostat_tick_event, 2000);

}

static void refresh_thermostat_tick(void){
  retrigger = false;
  sl_zigbee_af_event_set_inactive(&thermostat_tick_event);
  sl_zigbee_af_event_set_active(&thermostat_tick_event);
}

static void handle_button_event(const button_event_t *event){
  int16_t target_temp;
  uint8_t heating_enabled;
  const sl_zigbee_af_status_t status = get_target(&target_temp, &heating_enabled);

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

  retrigger = true;
}

static void update_decommission_watchdog(void){
  static bool decommission_hold_prev = false;

  if (decommission_started && !decommission_hold_prev) {
    sl_zigbee_af_event_set_active(&decommission_watch_event);
  }
  decommission_hold_prev = decommission_started;

  if (!decommission_started) {
    return;
  }

  const uint32_t now = now_ms();
  if (now > decommission_start_time + 3000) {
    retrigger = true;
  }
  if (now > decommission_start_time + 10000) {
    sl_zigbee_leave_network(SL_ZIGBEE_LEAVE_NWK_WITH_NO_OPTION);
    decommission_started = false;
    retrigger = true;
  }
}

void app_process_action(void)
{
  if (retrigger) {
    refresh_thermostat_tick();
  }

  button_event_t event;
  while (button_queue_pop(&event)) {
    handle_button_event(&event);
  }

  update_decommission_watchdog();
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
  UNUSED_VAR(value);

  if (endpoint == THERMOSTAT_ENDPOINT && clusterId == ZCL_THERMOSTAT_CLUSTER_ID && (attributeId== ZCL_SYSTEM_MODE_ATTRIBUTE_ID || attributeId == ZCL_OCCUPIED_HEATING_SETPOINT_ATTRIBUTE_ID)){
    retrigger = true;
  }
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
  retrigger = true;
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
  retrigger = true;
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