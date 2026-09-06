
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

sl_zigbee_af_event_t thermostat_tick_event;
void thermostat_tick();

bool retrigger = false;

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


  sl_zigbee_af_event_set_delay_ms(&thermostat_tick_event, 5000);
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
  
  sl_zigbee_af_event_set_delay_ms(&thermostat_tick_event, 10000);

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