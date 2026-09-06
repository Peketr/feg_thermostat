/***************************************************************************//**
 * @file app.c
 * @brief Callbacks implementation and application specific code.
 *******************************************************************************
 * # License
 * <b>Copyright 2021 Silicon Laboratories Inc. www.silabs.com</b>
 *******************************************************************************
 *
 * The licensor of this software is Silicon Laboratories Inc. Your use of this
 * software is governed by the terms of Silicon Labs Master Software License
 * Agreement (MSLA) available at
 * www.silabs.com/about-us/legal/master-software-license-agreement. This
 * software is distributed to you in Source Code format and is governed by the
 * sections of the MSLA applicable to Source Code.
 *
 ******************************************************************************/

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

bool steering = false;

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

// Display
#include "mikroe_ssd1351.h"
#include "glib.h"
#include "start_image.h"

#define THERMOSTAT_ENDPOINT 1
sl_zigbee_af_event_t thermostat_tick_event;
void thermostat_tick();

static bool retrigger = false;

static glib_context_t glib_context;

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


void update_measurement(uint32_t rh_data, int32_t temp_data){
  
  int16_t temp_to_send = temp_data / 10;
  uint16_t rh_to_send = rh_data / 10;

  sl_zigbee_af_status_t status =
    sl_zigbee_af_write_server_attribute(
      THERMOSTAT_ENDPOINT,
      ZCL_THERMOSTAT_CLUSTER_ID,
      ZCL_LOCAL_TEMPERATURE_ATTRIBUTE_ID,
      (uint8_t *)&temp_to_send,
      ZCL_INT16S_ATTRIBUTE_TYPE);

  if (status)
    sl_zigbee_app_debug_println("Thermostat measured value update error: 0x%x",status);

  status =
    sl_zigbee_af_write_server_attribute(
      THERMOSTAT_ENDPOINT,
      ZCL_TEMP_MEASUREMENT_CLUSTER_ID,
      ZCL_TEMP_MEASURED_VALUE_ATTRIBUTE_ID,
      (uint8_t *)&temp_to_send,
      ZCL_INT16S_ATTRIBUTE_TYPE);

  if (status)
    sl_zigbee_app_debug_println("Temperature measured value update error: 0x%x",status);

  status =
    sl_zigbee_af_write_server_attribute(
      THERMOSTAT_ENDPOINT,
      ZCL_RELATIVE_HUMIDITY_MEASUREMENT_CLUSTER_ID,
      ZCL_RELATIVE_HUMIDITY_MEASURED_VALUE_ATTRIBUTE_ID,
      (uint8_t *)&rh_to_send,
      ZCL_INT16U_ATTRIBUTE_TYPE);

  if (status)
    sl_zigbee_app_debug_println("RH measured value update error: 0x%x",status);
}

sl_zigbee_af_status_t set_target_temp(int16_t target_temp){
  if (target_temp < 700 || target_temp > 3000)
    return SL_ZIGBEE_ZCL_STATUS_INVALID_VALUE;
  sl_zigbee_af_status_t status = sl_zigbee_af_write_server_attribute(THERMOSTAT_ENDPOINT,
                                                                      ZCL_THERMOSTAT_CLUSTER_ID,
                                                                      ZCL_OCCUPIED_HEATING_SETPOINT_ATTRIBUTE_ID,
                                                                      (uint8_t*)&target_temp,
                                                                      ZCL_INT16S_ATTRIBUTE_TYPE);
  if (status)
    sl_zigbee_app_debug_println("Error setting setpoint: 0x%x",status);
  return status;
}

sl_zigbee_af_status_t set_system_mode(bool enable_heating){
  uint8_t system_mode = (enable_heating)?(0x04):(0x00);
  sl_zigbee_app_debug_println("Setting system mode to 0x%x",system_mode);
  sl_zigbee_af_status_t status = sl_zigbee_af_write_server_attribute(THERMOSTAT_ENDPOINT,
                                                                      ZCL_THERMOSTAT_CLUSTER_ID,
                                                                      ZCL_SYSTEM_MODE_ATTRIBUTE_ID,
                                                                      (uint8_t*)&system_mode,
                                                                      ZCL_ENUM8_ATTRIBUTE_TYPE);
  if (status)
    sl_zigbee_app_debug_println("Error setting system mode: 0x%x",status);
  return status;
}


sl_zigbee_af_status_t get_target(int16_t *target_temp,uint8_t *system_mode){
  
  sl_zigbee_af_status_t status = sl_zigbee_af_read_server_attribute(THERMOSTAT_ENDPOINT,
                                                         ZCL_THERMOSTAT_CLUSTER_ID,
                                                         ZCL_SYSTEM_MODE_ATTRIBUTE_ID,
                                                         (uint8_t*) system_mode,
                                                         sizeof(system_mode));
  if (status) 
    return status;

  status = sl_zigbee_af_read_server_attribute(THERMOSTAT_ENDPOINT,
                                                         ZCL_THERMOSTAT_CLUSTER_ID,
                                                         ZCL_OCCUPIED_HEATING_SETPOINT_ATTRIBUTE_ID,
                                                         (uint8_t*) target_temp,
                                                         sizeof(target_temp));
  

  return status;
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

uint8_t actuate_heating(int16_t current_temp, int16_t target_temp, bool enable){
  //Actuate Heating

  uint8_t valves_to_open = 0;
  static uint32_t time_of_last_change = 0;

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
      time_of_last_change = sl_sleeptimer_tick_to_ms(sl_sleeptimer_get_tick_count());
      break;  
  }


  return valves_to_open;
}

void draw_display(glib_context_t* glib_context, int16_t target_temp, int16_t current_temp, uint8_t open_valves, bool heating_enabled){
  glib_clear(glib_context);
  if(heating_enabled){
    glib_draw_char(glib_context,10,20,48+target_temp/1000,0xFFFF,0x0000,4,4);
    glib_draw_char(glib_context,34,20,48+(target_temp/100%10),0xFFFF,0x0000,4,4);
    glib_draw_char(glib_context,58,20,'.',0xFFFF,0x0000,4,4);
    glib_draw_char(glib_context,82,20,48+(target_temp/10%10),0xFFFF,0x0000,4,4);
  }

  glib_draw_char(glib_context,10,60,48+current_temp/1000,0xFFFF,0x0000,4,4);
  glib_draw_char(glib_context,34,60,48+(current_temp/100%10),0xFFFF,0x0000,4,4);
  glib_draw_char(glib_context,58,60,'.',0xFFFF,0x0000,4,4);
  glib_draw_char(glib_context,82,60,48+(current_temp/10%10),0xFFFF,0x0000,4,4);

  if (!heating_enabled){
    glib_draw_string(glib_context, "Off", 0, 0);
  } else if (open_valves == 0) {
    glib_draw_string(glib_context, "Idle", 0, 0);
  }else if (open_valves == 1){
    glib_draw_string(glib_context, "Half", 0, 0);
  }else if (open_valves == 2){
    glib_draw_string(glib_context, "Full", 0, 0);
  }

  if (steering) {
    glib_draw_string(glib_context, "Connecting", 65, 0);
  } else if (sl_zigbee_stack_is_up() && sl_zigbee_network_state() == SL_ZIGBEE_JOINED_NETWORK){
    glib_draw_string(glib_context, "Connected", 65, 0);
  } else {
    glib_draw_string(glib_context, "Disconnected", 40, 0);
  }
  
  glib_update_display();
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

  //Update Display
  draw_display(&glib_context, target_temp, current_temp, open_valves,enable_heating);


  sl_zigbee_af_event_set_delay_ms(&thermostat_tick_event, 10000);
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
  steering = false;
}

/** @brief
 *
 * Application framework equivalent of ::sl_zigbee_radio_needs_calibrating_handler
 */
void sl_zigbee_af_radio_needs_calibrating_cb(void)
{
  sl_mac_calibrate_current_channel();
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
  
  //  OLED initialization.
  sc = mikroe_ssd1351_init(sl_spidrv_inst0_handle);
  if (sc) {
    sl_zigbee_app_debug_println("Error initializing OLED> 0x%x", sc);
  } else {
    glib_init(&glib_context);
    glib_set_bg_color(&glib_context, 0x0000);
    glib_set_text_color(&glib_context, 0xFFFF);
    glib_enable_display(true);
    
    mikroe_ssd1351_image(ppcat128x128, 0, 0);
    glib_update_display();  
    
  }
  
  sl_zigbee_af_event_set_delay_ms(&thermostat_tick_event, 1000);

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
          sl_zigbee_app_debug_println("Handling Plus button");
          if(status == SL_ZIGBEE_ZCL_STATUS_SUCCESS && heating_enabled)
            set_target_temp(target_temp + 50);
          break;
        case BTNM:
          sl_zigbee_app_debug_println("Handling Minus button");
          if(status == SL_ZIGBEE_ZCL_STATUS_SUCCESS && heating_enabled)
            status = set_target_temp(target_temp - 50);
          sl_zigbee_app_debug_println("Handling Minus button result: 0x%x",status);
          break;
        case BTNA:
          sl_zigbee_app_debug_println("Handling A button");
          if (status == SL_ZIGBEE_ZCL_STATUS_SUCCESS)
            set_system_mode(!heating_enabled);
          break;
        case BTNC:
          if (sl_zigbee_stack_is_up() && sl_zigbee_network_state() == SL_ZIGBEE_JOINED_NETWORK) {
            if (button_pressed_very_long[i]){
              sl_zigbee_app_debug_println("Leaving Network due to long press");
              sl_zigbee_leave_network(SL_ZIGBEE_LEAVE_NWK_WITH_NO_OPTION);
            }
          } else {
            sl_zigbee_app_debug_println("Starting Commissioning due to button press");
            sl_zigbee_af_network_steering_start();
            steering = true;
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
  
}