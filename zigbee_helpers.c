
#include "zigbee_helpers.h"

#define THERMOSTAT_ENDPOINT 1

extern bool retrigger;
extern bool steering;

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

sl_zigbee_af_status_t update_running_state(uint8_t open_valves){
  uint16_t running_state = 0;
  if (open_valves > 0) running_state += 1;
  if (open_valves > 1) running_state += 8;
  sl_zigbee_af_status_t status = sl_zigbee_af_write_server_attribute(THERMOSTAT_ENDPOINT,
                                                                      ZCL_THERMOSTAT_CLUSTER_ID,
                                                                      ZCL_THERMOSTAT_RUNNING_STATE_ATTRIBUTE_ID,
                                                                      (uint8_t*)&running_state,
                                                                      ZCL_BITMAP16_ATTRIBUTE_TYPE);
  if (status)
    sl_zigbee_app_debug_println("Error setting system mode: 0x%x",status);
  return status;
}

bool on_network(){
   return sl_zigbee_stack_is_up() && sl_zigbee_network_state() == SL_ZIGBEE_JOINED_NETWORK;
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
