
#include "app/framework/include/af.h"
#include "network-formation.h"
#include "platform-header.h"
#include "sl_button.h"
#include "sl_simple_button.h"
#include "sl_sleeptimer.h"
#include "sl_spidrv_instances.h"
#include "sl_status.h"
#include "sl_zigbee_debug_print.h"
#include "sl_zigbee_system_common.h"
#include "zap-id.h"
#include "zap-type.h"
#include "zigbee_app_framework_event.h"
#include "zigbee_common_callback_dispatcher.h"
#include "network-steering.h"

#include "zigbee_helpers.h"
#include "gui.h"

// RHT Sensor
#include "sl_i2cspm_instances.h"
#include "sl_si70xx.h"
#include <stdbool.h>
#include <stdint.h>

// Buttons and Relays
#include "sl_simple_led_instances.h"
#include "sl_simple_button_instances.h"

#include "sl_sleeptimer.h"
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

uint8_t button_pressed_id[16] = {BTN_NONE};
bool button_pressed_long[16] = {false};
bool button_pressed_very_long[16] = {false};

sl_zigbee_af_event_t thermostat_tick_event;
void thermostat_tick();

bool retrigger = false;
bool decommission_started = false;
uint32_t decommission_start_time = 0;

glib_context_t glib_context;

uint8_t fetch_btn_id(const sl_button_t* handle){
  if (handle == &sl_button_btna){
    return BTNA;
  } else if (handle == &sl_button_btnb){
    return BTNB;
  } else if (handle == &sl_button_btnc){
    return BTNC;
  } else if (handle == &sl_button_btnd){
    return BTND;
  } else if (handle == &sl_button_btnplus){
    return BTNP;
  } else if (handle == &sl_button_btnminus){
    return BTNM;
  } 
  return BTN_NONE;
}

void sl_button_on_change(const sl_button_t *handle){

  static bool pressed = false;
  static uint8_t btn = 0;
  static uint32_t pressbegin = 0;

  if (!pressed){ // if button press didn't start yet
    if (handle->get_state(handle) == SL_SIMPLE_BUTTON_PRESSED){
      pressed = true;
      btn = fetch_btn_id(handle);
      pressbegin = sl_sleeptimer_tick_to_ms(sl_sleeptimer_get_tick_count());
    }
  } else { // if already pressed
    if (handle->get_state(handle) == SL_SIMPLE_BUTTON_RELEASED && fetch_btn_id(handle) == btn){
      static uint8_t index = 0;
      sl_zigbee_app_debug_println("registering button press %d",index);
      button_pressed_id[index] = btn;
      button_pressed_long[index] = (sl_sleeptimer_tick_to_ms(sl_sleeptimer_get_tick_count()) - pressbegin) > 1000;
      button_pressed_very_long[index] = (sl_sleeptimer_tick_to_ms(sl_sleeptimer_get_tick_count()) - pressbegin) > 10000;

      index = (index + 1) % 16;
      btn = BTN_NONE;
      pressed = false;
      
    }
  }

  if (handle == &sl_button_btnc){
    if (on_network() && handle->get_state(handle) == SL_SIMPLE_BUTTON_PRESSED){
      decommission_started = true;
      decommission_start_time = sl_sleeptimer_tick_to_ms(sl_sleeptimer_get_tick_count());
    } else {
      decommission_started = false;
    }
  }
  if (handle == &sl_button_btna){
    sl_zigbee_app_debug_print("Button A ");
  } else if (handle == &sl_button_btnb){
    sl_zigbee_app_debug_print("Button B ");
  } else if (handle == &sl_button_btnc){
    sl_zigbee_app_debug_print("Button C ");
  } else if (handle == &sl_button_btnd){
    sl_zigbee_app_debug_print("Button D ");
  } else if (handle == &sl_button_btnplus){
    sl_zigbee_app_debug_print("Button Plus ");
  } else if (handle == &sl_button_btnminus){
    sl_zigbee_app_debug_print("Button Minus ");
  } 
  if (handle->get_state(handle) == SL_SIMPLE_BUTTON_PRESSED){
    sl_zigbee_app_debug_println("Pressed");
  } else {
    sl_zigbee_app_debug_println("Released");
  }
}


uint8_t actuate_heating(int16_t current_temp, int16_t target_temp, bool enable){
  //Actuate Heating

  uint8_t valves_to_open = 0;

  if ( enable ){
    if (current_temp < target_temp - 50){
      valves_to_open = 2;
    } else {
      if (current_temp < target_temp + 50) {
        valves_to_open = 1;
      } else {
        valves_to_open = 0;
      }
    }
  } else {
    //DISABLE BOTH VALVES, something is not right!!
    valves_to_open = 0;
  }
  sl_zigbee_app_debug_println("valves_to_open: %d",valves_to_open);

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
void thermostat_tick(){
  sl_status_t sc;
  uint32_t rh_data;
  int32_t temp_data;
  int16_t target_temp;
  uint8_t system_mode;

  // Measure RHT
  sc = sl_si70xx_measure_rh_and_temp(sl_i2cspm_inst0, SI7021_ADDR, &rh_data, &temp_data);
  if (sc)
    sl_zigbee_app_debug_println("error: 0x%x",sc);
  else {
    sl_zigbee_app_debug_println("periodic: %d,%d" , rh_data/1000, temp_data/1000);
    //Update RHT reading
    update_measurement(rh_data, temp_data);

  }

  sl_zigbee_af_status_t status = get_target(&target_temp, &system_mode);
  if (status)
    sl_zigbee_app_debug_println("Target read error: 0x%x",status);
  sl_zigbee_app_debug_println("target temperature: %d, mode: 0x%x" , target_temp/100, system_mode);

  // Actuate Relays
  int16_t current_temp = temp_data / 10;
  bool enable_heating = status == SL_ZIGBEE_ZCL_STATUS_SUCCESS && sc == SL_STATUS_OK && system_mode == 0x04;
  uint8_t open_valves = actuate_heating(current_temp, target_temp,enable_heating);

  // Update relay state to gateway
  update_running_state(open_valves);

  //Update Display
  draw_display(&glib_context, target_temp, current_temp, open_valves,enable_heating);


  sl_zigbee_af_event_set_delay_ms(&thermostat_tick_event, 30000);
}

void handle_button_press(){
  
}


// APPLICATION FRAMEWORK FUNCTIONS

void app_init(){
  sl_status_t sc;

  sc = sl_si70xx_init(sl_i2cspm_inst0, SI7021_ADDR);
  if (sc)
    sl_zigbee_app_debug_println(" init error: 0x%x",sc);

  
  sl_zigbee_af_event_init(&thermostat_tick_event, thermostat_tick);
  
  oled_init(&glib_context);
  retrigger = false;
  
  sl_zigbee_af_event_set_delay_ms(&thermostat_tick_event, 2000);

}

void app_process_action(void)
{
  if (retrigger) {
    retrigger = false;
    sl_zigbee_af_event_set_inactive(&thermostat_tick_event);
    sl_zigbee_af_event_set_active(&thermostat_tick_event);
  }

  static uint8_t i = 0;
  bool ran_once = false;
  while (button_pressed_id[i]!=BTN_NONE){

      if (button_pressed_id[i]){
      int16_t target_temp;
      uint8_t heating_enabled; // system_mode
      sl_zigbee_af_status_t status = get_target(&target_temp, &heating_enabled);

      switch (button_pressed_id[i]){
        case BTNP:
          if(status == SL_ZIGBEE_ZCL_STATUS_SUCCESS && heating_enabled)
            set_target_temp(target_temp + 50);
          break;
        case BTNM:
          if(status == SL_ZIGBEE_ZCL_STATUS_SUCCESS && heating_enabled)
            status = set_target_temp(target_temp - 50);
          break;
        case BTNA:
          if (status == SL_ZIGBEE_ZCL_STATUS_SUCCESS)
            set_system_mode(!heating_enabled);
          break;
        case BTNC:
          if (!on_network()) {
            sl_zigbee_af_network_steering_start();
            retrigger = true;
          }
          break;
      }
      button_pressed_id[i] = BTN_NONE;  
    }
    i = (i + 1) % 16;
  }
  if (ran_once){
    sl_zigbee_af_event_set_inactive(&thermostat_tick_event);
    sl_zigbee_af_event_set_active(&thermostat_tick_event);
  }
  if (decommission_started && sl_sleeptimer_tick_to_ms(sl_sleeptimer_get_tick_count()) > decommission_start_time + 3000){
    retrigger = true;
  }
  if (decommission_started && sl_sleeptimer_tick_to_ms(sl_sleeptimer_get_tick_count()) > decommission_start_time + 10000){
    sl_zigbee_leave_network(SL_ZIGBEE_LEAVE_NWK_WITH_NO_OPTION);
    decommission_started = false;
    retrigger = true;
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
