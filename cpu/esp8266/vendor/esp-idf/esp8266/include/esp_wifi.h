// Copyright 2018 Espressif Systems (Shanghai) PTE LTD
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at

//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.


/*               Notes about WiFi Programming
 *
 *  The ESP8266 WiFi programming model can be depicted as following picture:
 *
 *
 *                            default handler              user handler
 *  -------------             ---------------             ---------------
 *  |           |   event     |             | callback or |             |
 *  |   tcpip   | --------->  |    event    | ----------> | application |
 *  |   stack   |             |     task    |  event      |    task     |
 *  |-----------|             |-------------|             |-------------|
 *                                  /|\                          |
 *                                   |                           |
 *                            event  |                           |
 *                                   |                           |
 *                                   |                           |
 *                             ---------------                   |
 *                             |             |                   |
 *                             | WiFi Driver |/__________________|
 *                             |             |\     API call
 *                             |             |
 *                             |-------------|
 *
 * The WiFi driver can be consider as black box, it knows nothing about the high layer code, such as
 * TCPIP stack, application task, event task etc, all it can do is to receive API call from high layer
 * or post event queue to a specified Queue, which is initialized by API esp_wifi_init().
 *
 * The event task is a daemon task, which receives events from WiFi driver or from other subsystem, such
 * as TCPIP stack, event task will call the default callback function on receiving the event. For example,
 * on receiving event SYSTEM_EVENT_STA_CONNECTED, it will call tcpip_adapter_start() to start the DHCP
 * client in it's default handler.
 *
 * Application can register it's own event callback function by API esp_event_init, then the application callback
 * function will be called after the default callback. Also, if application doesn't want to execute the callback
 * in the event task, what it needs to do is to post the related event to application task in the application callback function.
 *
 * The application task (code) generally mixes all these thing together, it calls APIs to init the system/WiFi and
 * handle the events when necessary.
 *
 */

#ifndef __ESP_WIFI_H__
#define __ESP_WIFI_H__

#include <stdint.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "sdkconfig.h"
#include "esp_err.h"
#include "esp_wifi_types.h"
#include "esp_event.h"
#include "esp_wifi_os_adapter.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ESP_ERR_WIFI_NOT_INIT    (ESP_ERR_WIFI_BASE + 1)   /*!< WiFi driver was not installed by esp_wifi_init */
#define ESP_ERR_WIFI_NOT_STARTED (ESP_ERR_WIFI_BASE + 2)   /*!< WiFi driver was not started by esp_wifi_start */
#define ESP_ERR_WIFI_NOT_STOPPED (ESP_ERR_WIFI_BASE + 3)   /*!< WiFi driver was not stopped by esp_wifi_stop */
#define ESP_ERR_WIFI_IF          (ESP_ERR_WIFI_BASE + 4)   /*!< WiFi interface error */
#define ESP_ERR_WIFI_MODE        (ESP_ERR_WIFI_BASE + 5)   /*!< WiFi mode error */
#define ESP_ERR_WIFI_STATE       (ESP_ERR_WIFI_BASE + 6)   /*!< WiFi internal state error */
#define ESP_ERR_WIFI_CONN        (ESP_ERR_WIFI_BASE + 7)   /*!< WiFi internal control block of station or soft-AP error */
#define ESP_ERR_WIFI_NVS         (ESP_ERR_WIFI_BASE + 8)   /*!< WiFi internal NVS module error */
#define ESP_ERR_WIFI_MAC         (ESP_ERR_WIFI_BASE + 9)   /*!< MAC address is invalid */
#define ESP_ERR_WIFI_SSID        (ESP_ERR_WIFI_BASE + 10)   /*!< SSID is invalid */
#define ESP_ERR_WIFI_PASSWORD    (ESP_ERR_WIFI_BASE + 11)  /*!< Password is invalid */
#define ESP_ERR_WIFI_TIMEOUT     (ESP_ERR_WIFI_BASE + 12)  /*!< Timeout error */
#define ESP_ERR_WIFI_WAKE_FAIL   (ESP_ERR_WIFI_BASE + 13)  /*!< WiFi is in sleep state(RF closed) and wakeup fail */
#define ESP_ERR_WIFI_WOULD_BLOCK (ESP_ERR_WIFI_BASE + 14)  /*!< The caller would block */
#define ESP_ERR_WIFI_NOT_CONNECT (ESP_ERR_WIFI_BASE + 15)  /*!< Station still in disconnect status */

#define ESP_WIFI_PARAM_USE_NVS  0

/**
 * @brief WiFi stack configuration parameters passed to esp_wifi_init call.
 */
typedef struct {
    system_event_handler_t event_handler;          /**< WiFi event handler */
    wifi_osi_funcs_t*      osi_funcs;              /**< WiFi OS functions */
    int                    static_rx_buf_num;      /**< WiFi static RX buffer number */
    int                    dynamic_rx_buf_num;     /**< WiFi dynamic RX buffer number */
    int                    tx_buf_type;            /**< WiFi TX buffer type */
    int                    static_tx_buf_num;      /**< WiFi static TX buffer number */
    int                    dynamic_tx_buf_num;     /**< WiFi dynamic TX buffer number */
    int                    csi_enable;             /**< WiFi channel state information enable flag */
    int                    ampdu_rx_enable;        /**< WiFi AMPDU RX feature enable flag */
    int                    ampdu_tx_enable;        /**< WiFi AMPDU TX feature enable flag */
    int                    nvs_enable;             /**< WiFi NVS flash enable flag */
    int                    nano_enable;            /**< Nano option for printf/scan family enable flag */
    int                    tx_ba_win;              /**< WiFi Block Ack TX window size */
    int                    rx_ba_win;              /**< WiFi Block Ack RX window size */
    int                    magic;                  /**< WiFi init magic number, it should be the last field */
} wifi_init_config_t;

#define WIFI_INIT_CONFIG_MAGIC    0x1F2F3F4F

extern wifi_osi_funcs_t s_wifi_osi_funcs;
#define WIFI_INIT_CONFIG_DEFAULT() { \
    .event_handler = &esp_event_send, \
    .osi_funcs = &s_wifi_osi_funcs, \
    .static_rx_buf_num = 5,\
    .dynamic_rx_buf_num = 0,\
    .tx_buf_type = 0,\
    .static_tx_buf_num = 6,\
    .dynamic_tx_buf_num = 0,\
    .csi_enable = 0,\
    .ampdu_rx_enable = 0,\
    .ampdu_tx_enable = 0,\
    .nvs_enable = 1,\
    .nano_enable = 0,\
    .tx_ba_win = 0,\
    .rx_ba_win = 0,\
    .magic = WIFI_INIT_CONFIG_MAGIC\
};

/**
  * @brief  Init WiFi
  *         Alloc resource for WiFi driver, such as WiFi control structure, RX/TX buffer,
  *         WiFi NVS structure etc, this WiFi also start WiFi task
  *
  * @attention 1. This API must be called before all other WiFi API can be called
  * @attention 2. Always use WIFI_INIT_CONFIG_DEFAULT macro to init the config to default values, this can
  *               guarantee all the fields got correct value when more fields are added into wifi_init_config_t
  *               in future release. If you want to set your owner initial values, overwrite the default values
  *               which are set by WIFI_INIT_CONFIG_DEFAULT, please be notified that the field 'magic' of
  *               wifi_init_config_t should always be WIFI_INIT_CONFIG_MAGIC!
  *
  * @param  config pointer to WiFi init configuration structure; can point to a temporary variable.
  *
  * @return
  *    - ESP_OK: succeed
  *    - ESP_ERR_NO_MEM: out of memory
  *    - others: refer to error code esp_err.h
  */
esp_err_t esp_wifi_init(const wifi_init_config_t *config);

/**
  * @brief  Deinit WiFi
  *         Free all resource allocated in esp_wifi_init and stop WiFi task
  *
  * @attention 1. This API should be called if you want to remove WiFi driver from the system
  *
  * @return ESP_OK: succeed
  */
esp_err_t esp_wifi_deinit(void);

/**
  * @brief     Set the WiFi operating mode
  *
  *            Set the WiFi operating mode as station, soft-AP or station+soft-AP,
  *            The default mode is soft-AP mode.
  *
  * @param     mode  WiFi operating mode
  *
  * @return
  *    - ESP_OK: succeed
  *    - ESP_ERR_WIFI_NOT_INIT: WiFi is not initialized by esp_wifi_init
  *    - ESP_ERR_INVALID_ARG: invalid argument
  *    - others: refer to error code in esp_err.h
  */
esp_err_t esp_wifi_set_mode(wifi_mode_t mode);

/**
  * @brief  Get current operating mode of WiFi
  *
  * @param[out]  mode  store current WiFi mode
  *
  * @return
  *    - ESP_OK: succeed
  *    - ESP_ERR_WIFI_NOT_INIT: WiFi is not initialized by esp_wifi_init
  *    - ESP_ERR_INVALID_ARG: invalid argument
  */
esp_err_t esp_wifi_get_mode(wifi_mode_t *mode);

/**
  * @brief  Start WiFi according to current configuration
  *         If mode is WIFI_MODE_STA, it create station control block and start station
  *         If mode is WIFI_MODE_AP, it create soft-AP control block and start soft-AP
  *         If mode is WIFI_MODE_APSTA, it create soft-AP and station control block and start soft-AP and station
  *
  * @return
  *    - ESP_OK: succeed
  *    - ESP_ERR_WIFI_NOT_INIT: WiFi is not initialized by esp_wifi_init
  *    - ESP_ERR_INVALID_ARG: invalid argument
  *    - ESP_ERR_NO_MEM: out of memory
  *    - ESP_ERR_WIFI_CONN: WiFi internal error, station or soft-AP control block wrong
  *    - ESP_FAIL: other WiFi internal errors
  */
esp_err_t esp_wifi_start(void);

/**
  * @brief  Stop WiFi
  *         If mode is WIFI_MODE_STA, it stop station and free station control block
  *         If mode is WIFI_MODE_AP, it stop soft-AP and free soft-AP control block
  *         If mode is WIFI_MODE_APSTA, it stop station/soft-AP and free station/soft-AP control block
  *
  * @return
  *    - ESP_OK: succeed
  *    - ESP_ERR_WIFI_NOT_INIT: WiFi is not initialized by esp_wifi_init
  */
esp_err_t esp_wifi_stop(void);

/**
 * @brief  Restore WiFi stack persistent settings to default values
 *
 * This function will reset settings made using the following APIs:
 * - esp_wifi_get_auto_connect,
 * - esp_wifi_set_protocol,
 * - esp_wifi_set_config related
 * - esp_wifi_set_mode
 *
 * @return
 *    - ESP_OK: succeed
 *    - ESP_ERR_WIFI_NOT_INIT: WiFi is not initialized by esp_wifi_init
 */
esp_err_t esp_wifi_restore(void);

/**
  * @brief     Connect the ESP8266 WiFi station to the AP.
  *
  * @attention 1. This API only impact WIFI_MODE_STA or WIFI_MODE_APSTA mode
  * @attention 2. If the ESP8266 is connected to an AP, call esp_wifi_disconnect to disconnect.
  *
  * @return
  *    - ESP_OK: succeed
  *    - ESP_ERR_WIFI_NOT_INIT: WiFi is not initialized by esp_wifi_init
  *    - ESP_ERR_WIFI_NOT_START: WiFi is not started by esp_wifi_start
  *    - ESP_ERR_WIFI_CONN: WiFi internal error, station or soft-AP control block wrong
  *    - ESP_ERR_WIFI_SSID: SSID of AP which station connects is invalid
  */
esp_err_t esp_wifi_connect(void);

/**
  * @brief     Disconnect the ESP8266 WiFi station from the AP.
  *
  * @return
  *    - ESP_OK: succeed
  *    - ESP_ERR_WIFI_NOT_INIT: WiFi was not initialized by esp_wifi_init
  *    - ESP_ERR_WIFI_NOT_STARTED: WiFi was not started by esp_wifi_start
  *    - ESP_FAIL: other WiFi internal errors
  */
esp_err_t esp_wifi_disconnect(void);

/**
  * @brief     Currently this API is just an stub API
  *

  * @return
  *    - ESP_OK: succeed
  *    - others: fail
  */
esp_err_t esp_wifi_clear_fast_connect(void);

/**
  * @brief     deauthenticate all stations or associated id equals to aid
  *
  * @param     aid  when aid is 0, deauthenticate all stations, otherwise deauthenticate station whose associated id is aid
  *
  * @return
  *    - ESP_OK: succeed
  *    - ESP_ERR_WIFI_NOT_INIT: WiFi is not initialized by esp_wifi_init
  *    - ESP_ERR_WIFI_NOT_STARTED: WiFi was not started by esp_wifi_start
  *    - ESP_ERR_INVALID_ARG: invalid argument
  *    - ESP_ERR_WIFI_MODE: WiFi mode is wrong
  */
esp_err_t esp_wifi_deauth_sta(uint16_t aid);

/**
  * @brief     Scan all available APs.
  *
  * @attention If this API is called, the found APs are stored in WiFi driver dynamic allocated memory and the
  *            will be freed in esp_wifi_scan_get_ap_records, so generally, call esp_wifi_scan_get_ap_records to cause
  *            the memory to be freed once the scan is done
  * @attention The values of maximum active scan time and passive scan time per channel are limited to 1500 milliseconds.
  *            Values above 1500ms may cause station to disconnect from AP and are not recommended.
  *
  * @param     config  configuration of scanning
  * @param     block if block is true, this API will block the caller until the scan is done, otherwise
  *                         it will return immediately
  *
  * @return
  *    - ESP_OK: succeed
  *    - ESP_ERR_WIFI_NOT_INIT: WiFi is not initialized by esp_wifi_init
  *    - ESP_ERR_WIFI_NOT_STARTED: WiFi was not started by esp_wifi_start
  *    - ESP_ERR_WIFI_TIMEOUT: blocking scan is timeout
  *    - others: refer to error code in esp_err.h
  */
esp_err_t esp_wifi_scan_start(const wifi_scan_config_t *config, bool block);

/**
  * @brief     Stop the scan in process
  *
  * @return
  *    - ESP_OK: succeed
  *    - ESP_ERR_WIFI_NOT_INIT: WiFi is not initialized by esp_wifi_init
  *    - ESP_ERR_WIFI_NOT_STARTED: WiFi is not started by esp_wifi_start
  */
esp_err_t esp_wifi_scan_stop(void);

/**
  * @brief     Get number of APs found in last scan
  *
  * @param[out] number  store number of APIs found in last scan
  *
  * @attention This API can only be called when the scan is completed, otherwise it may get wrong value.
  *
  * @return
  *    - ESP_OK: succeed
  *    - ESP_ERR_WIFI_NOT_INIT: WiFi is not initialized by esp_wifi_init
  *    - ESP_ERR_WIFI_NOT_STARTED: WiFi is not started by esp_wifi_start
  *    - ESP_ERR_INVALID_ARG: invalid argument
  */
esp_err_t esp_wifi_scan_get_ap_num(uint16_t *number);

/**
  * @brief     Get AP list found in last scan
  *
  * @param[in,out] number       As input param, it stores max AP number ap_records can hold.
  *                             As output param, it receives the actual AP number this API returns.
  * @param         ap_records   wifi_ap_record_t array to hold the found APs
  *
  * @return
  *    - ESP_OK: succeed
  *    - ESP_ERR_WIFI_NOT_INIT: WiFi is not initialized by esp_wifi_init
  *    - ESP_ERR_WIFI_NOT_STARTED: WiFi is not started by esp_wifi_start
  *    - ESP_ERR_INVALID_ARG: invalid argument
  *    - ESP_ERR_NO_MEM: out of memory
  */
esp_err_t esp_wifi_scan_get_ap_records(uint16_t *number, wifi_ap_record_t *ap_records);


/**
  * @brief     Get information of AP which the ESP8266 station is associated with
  *
  * @param     ap_info  the wifi_ap_record_t to hold AP information
  *            sta can get the connected ap's phy mode info through the struct member
  *            phy_11b，phy_11g，phy_11n，phy_lr in the wifi_ap_record_t struct.
  *            For example, phy_11b = 1 imply that ap support 802.11b mode
  *
  * @return
  *    - ESP_OK: succeed
  *    - ESP_ERR_WIFI_CONN: The station interface don't initialized
  *    - ESP_ERR_WIFI_NOT_CONNECT: The station is in disconnect status
  */
esp_err_t esp_wifi_sta_get_ap_info(wifi_ap_record_t *ap_info);

/**
  * @brief     Set current power save type
  *
  * @attention Default power save type is WIFI_PS_NONE.
  *
  * @param     type  power save type
  *
  * @return    ESP_ERR_NOT_SUPPORTED: not supported yet
  */
esp_err_t esp_wifi_set_ps(wifi_ps_type_t type);

/**
  * @brief     Get current power save type
  *
  * @attention Default power save type is WIFI_PS_NONE.
  *
  * @param[out]  type: store current power save type
  *
  * @return    ESP_ERR_NOT_SUPPORTED: not supported yet
  */
esp_err_t esp_wifi_get_ps(wifi_ps_type_t *type);

/**
  * @brief     Set protocol type of specified interface
  *            The default protocol is (WIFI_PROTOCOL_11B|WIFI_PROTOCOL_11G|WIFI_PROTOCOL_11N)
  *
  * @attention Currently we only support 802.11b or 802.11bg or 802.11bgn mode
  *
  * @param     ifx  interfaces
  * @param     protocol_bitmap  WiFi protocol bitmap
  *
  * @return
  *    - ESP_OK: succeed
  *    - ESP_ERR_WIFI_NOT_INIT: WiFi is not initialized by esp_wifi_init
  *    - ESP_ERR_WIFI_IF: invalid interface
  *    - others: refer to error codes in esp_err.h
  */
esp_err_t esp_wifi_set_protocol(wifi_interface_t ifx, uint8_t protocol_bitmap);

/**
  * @brief     Get the current protocol bitmap of the specified interface
  *
  * @param     ifx  interface
  * @param[out] protocol_bitmap  store current WiFi protocol bitmap of interface ifx
  *
  * @return
  *    - ESP_OK: succeed
  *    - ESP_ERR_WIFI_NOT_INIT: WiFi is not initialized by esp_wifi_init
  *    - ESP_ERR_WIFI_IF: invalid interface
  *    - ESP_ERR_INVALID_ARG: invalid argument
  *    - others: refer to error codes in esp_err.h
  */
esp_err_t esp_wifi_get_protocol(wifi_interface_t ifx, uint8_t *protocol_bitmap);

/**
  * @brief     Set the bandwidth of ESP8266 specified interface
  *
  * @attention 1. API return false if try to configure an interface that is not enabled
  * @attention 2. WIFI_BW_HT40 is supported only when the interface support 11N
  *
  * @param     ifx  interface to be configured
  * @param     bw  bandwidth
  *
  * @return
  *    - ESP_OK: succeed
  *    - ESP_ERR_WIFI_NOT_INIT: WiFi is not initialized by esp_wifi_init
  *    - ESP_ERR_WIFI_IF: invalid interface
  *    - ESP_ERR_INVALID_ARG: invalid argument
  *    - others: refer to error codes in esp_err.h
  */
esp_err_t esp_wifi_set_bandwidth(wifi_interface_t ifx, wifi_bandwidth_t bw);

/**
  * @brief     Get the bandwidth of ESP8266 specified interface
  *
  * @attention 1. API return false if try to get a interface that is not enable
  *
  * @param     ifx interface to be configured
  * @param[out] bw  store bandwidth of interface ifx
  *
  * @return
  *    - ESP_OK: succeed
  *    - ESP_ERR_WIFI_NOT_INIT: WiFi is not initialized by esp_wifi_init
  *    - ESP_ERR_WIFI_IF: invalid interface
  *    - ESP_ERR_INVALID_ARG: invalid argument
  */
esp_err_t esp_wifi_get_bandwidth(wifi_interface_t ifx, wifi_bandwidth_t *bw);

/**
  * @brief     Set primary/secondary channel of ESP8266
  *
  * @attention 1. This is a special API for sniffer
  * @attention 2. This API should be called after esp_wifi_start() or esp_wifi_set_promiscuous()
  *
  * @param     primary  for HT20, primary is the channel number, for HT40, primary is the primary channel
  * @param     second   for HT20, second is ignored, for HT40, second is the second channel
  *
  * @return
  *    - ESP_OK: succeed
  *    - ESP_ERR_WIFI_NOT_INIT: WiFi is not initialized by esp_wifi_init
  *    - ESP_ERR_WIFI_IF: invalid interface
  *    - ESP_ERR_INVALID_ARG: invalid argument
  */
esp_err_t esp_wifi_set_channel(uint8_t primary, wifi_second_chan_t second);

/**
  * @brief     Get the primary/secondary channel of ESP8266
  *
  * @attention 1. API return false if try to get a interface that is not enable
  *
  * @param     primary   store current primary channel
  * @param[out]  second  store current second channel
  *
  * @return
  *    - ESP_OK: succeed
  *    - ESP_ERR_WIFI_NOT_INIT: WiFi is not initialized by esp_wifi_init
  *    - ESP_ERR_INVALID_ARG: invalid argument
  */
esp_err_t esp_wifi_get_channel(uint8_t *primary, wifi_second_chan_t *second);

/**
  * @brief     configure country info
  *
  * @attention 1. The default country is {.cc="CN", .schan=1, .nchan=13, policy=WIFI_COUNTRY_POLICY_AUTO}
  * @attention 2. When the country policy is WIFI_COUNTRY_POLICY_AUTO, the country info of the AP to which
  *               the station is connected is used. E.g. if the configured country info is {.cc="USA", .schan=1, .nchan=11}
  *               and the country info of the AP to which the station is connected is {.cc="JP", .schan=1, .nchan=14}
  *               then the country info that will be used is {.cc="JP", .schan=1, .nchan=14}. If the station disconnected
  *               from the AP the country info is set back back to the country info of the station automatically,
  *               {.cc="USA", .schan=1, .nchan=11} in the example.
  * @attention 3. When the country policy is WIFI_COUNTRY_POLICY_MANUAL, always use the configured country info.
  * @attention 4. When the country info is changed because of configuration or because the station connects to a different
  *               external AP, the country IE in probe response/beacon of the soft-AP is changed also.
  * @attention 5. The country configuration is not stored into flash
  * @attention 6. This API doesn't validate the per-country rules, it's up to the user to fill in all fields according to
  *               local regulations.
  *
  * @param     country   the configured country info
  *
  * @return
  *    - ESP_OK: succeed
  *    - ESP_ERR_WIFI_NOT_INIT: WiFi is not initialized by esp_wifi_init
  *    - ESP_ERR_INVALID_ARG: invalid argument
  */
esp_err_t esp_wifi_set_country(const wifi_country_t *country);

/**
  * @brief     get the current country info
  *
  * @param     country  country info
  *
  * @return
  *    - ESP_OK: succeed
  *    - ESP_ERR_WIFI_NOT_INIT: WiFi is not initialized by esp_wifi_init
  *    - ESP_ERR_INVALID_ARG: invalid argument
  */
esp_err_t esp_wifi_get_country(wifi_country_t *country);


/**
  * @brief     Set MAC address of the ESP8266 WiFi station or the soft-AP interface.
  *
  * @attention 1. This API can only be called when the interface is disabled
  * @attention 2. ESP8266 soft-AP and station have different MAC addresses, do not set them to be the same.
  * @attention 3. The bit 0 of the first byte of ESP8266 MAC address can not be 1. For example, the MAC address
  *      can set to be "1a:XX:XX:XX:XX:XX", but can not be "15:XX:XX:XX:XX:XX".
  *
  * @param     ifx  interface
  * @param     mac  the MAC address
  *
  * @return
  *    - ESP_OK: succeed
  *    - ESP_ERR_WIFI_NOT_INIT: WiFi is not initialized by esp_wifi_init
  *    - ESP_ERR_INVALID_ARG: invalid argument
  *    - ESP_ERR_WIFI_IF: invalid interface
  *    - ESP_ERR_WIFI_MAC: invalid mac address
  *    - ESP_ERR_WIFI_MODE: WiFi mode is wrong
  *    - others: refer to error codes in esp_err.h
  */
esp_err_t esp_wifi_set_mac(wifi_interface_t ifx, const uint8_t mac[6]);

/**
  * @brief     Get mac of specified interface
  *
  * @param      ifx  interface
  * @param[out] mac  store mac of the interface ifx
  *
  * @return
  *    - ESP_OK: succeed
  *    - ESP_ERR_WIFI_NOT_INIT: WiFi is not initialized by esp_wifi_init
  *    - ESP_ERR_INVALID_ARG: invalid argument
  *    - ESP_ERR_WIFI_IF: invalid interface
  */
esp_err_t esp_wifi_get_mac(wifi_interface_t ifx, uint8_t mac[6]);

/**
  * @brief The RX callback function in the promiscuous mode.
  *        Each time a packet is received, the callback function will be called.
  *
  * @param buf  Data received. Type of data in buffer (wifi_promiscuous_pkt_t or wifi_pkt_rx_ctrl_t) indicated by 'type' parameter.
  * @param type  promiscuous packet type.
  *
  */
typedef void (* wifi_promiscuous_cb_t)(void *buf, wifi_promiscuous_pkt_type_t type);
/**
  * @brief Register the RX callback function in the promiscuous mode.
  *
  * Each time a packet is received, the registered callback function will be called.
  *
  * @param cb  callback
  *
  * @return
  *    - ESP_OK: succeed
  *    - ESP_ERR_WIFI_NOT_INIT: WiFi is not initialized by esp_wifi_init
  */
esp_err_t esp_wifi_set_promiscuous_rx_cb(wifi_promiscuous_cb_t cb);

/**
  * @brief     Enable the promiscuous mode.
  *
  * @param     en  false - disable, true - enable
  *
  * @return
  *    - ESP_OK: succeed
  *    - ESP_ERR_WIFI_NOT_INIT: WiFi is not initialized by esp_wifi_init
  */
esp_err_t esp_wifi_set_promiscuous(bool en);

/**
  * @brief     Get the promiscuous mode.
  *
  * @param[out] en  store the current status of promiscuous mode
  *
  * @return
  *    - ESP_OK: succeed
  *    - ESP_ERR_WIFI_NOT_INIT: WiFi is not initialized by esp_wifi_init
  *    - ESP_ERR_INVALID_ARG: invalid argument
  */
esp_err_t esp_wifi_get_promiscuous(bool *en);

/**
  * @brief Enable the promiscuous mode packet type filter.
  *
  * @note The default filter is to filter all packets except WIFI_PKT_MISC
  *
  * @param filter the packet type filtered in promiscuous mode.
  *
  * @return
  *    - ESP_OK: succeed
  *    - ESP_ERR_WIFI_NOT_INIT: WiFi is not initialized by esp_wifi_init
  */
esp_err_t esp_wifi_set_promiscuous_filter(const wifi_promiscuous_filter_t *filter);

/**
  * @brief     Get the promiscuous filter.
  *
  * @param[out] filter  store the current status of promiscuous filter
  *
  * @return
  *    - ESP_OK: succeed
  *    - ESP_ERR_WIFI_NOT_INIT: WiFi is not initialized by esp_wifi_init
  *    - ESP_ERR_INVALID_ARG: invalid argument
  */
esp_err_t esp_wifi_get_promiscuous_filter(wifi_promiscuous_filter_t *filter);

/**
  * @brief     Set the configuration of the ESP8266 STA or AP
  *
  * @attention 1. This API can be called only when specified interface is enabled, otherwise, API fail
  * @attention 2. For station configuration, bssid_set needs to be 0; and it needs to be 1 only when users need to check the MAC address of the AP.
  * @attention 3. ESP8266 is limited to only one channel, so when in the soft-AP+station mode, the soft-AP will adjust its channel automatically to be the same as
  *               the channel of the ESP8266 station.
  *
  * @param     interface  interface
  * @param     conf  station or soft-AP configuration
  *
  * @return
  *    - ESP_OK: succeed
  *    - ESP_ERR_WIFI_NOT_INIT: WiFi is not initialized by esp_wifi_init
  *    - ESP_ERR_INVALID_ARG: invalid argument
  *    - ESP_ERR_WIFI_IF: invalid interface
  *    - ESP_ERR_WIFI_MODE: invalid mode
  *    - ESP_ERR_WIFI_PASSWORD: invalid password
  *    - ESP_ERR_WIFI_NVS: WiFi internal NVS error
  *    - others: refer to the erro code in esp_err.h
  */
esp_err_t esp_wifi_set_config(wifi_interface_t interface, wifi_config_t *conf);

/**
  * @brief     Get configuration of specified interface
  *
  * @param     interface  interface
  * @param[out]  conf  station or soft-AP configuration
  *
  * @return
  *    - ESP_OK: succeed
  *    - ESP_ERR_WIFI_NOT_INIT: WiFi is not initialized by esp_wifi_init
  *    - ESP_ERR_INVALID_ARG: invalid argument
  *    - ESP_ERR_WIFI_IF: invalid interface
  */
esp_err_t esp_wifi_get_config(wifi_interface_t interface, wifi_config_t *conf);

/**
  * @brief     Get STAs associated with soft-AP
  *
  * @attention SSC only API
  *
  * @param[out] sta  station list
  *             ap can get the connected sta's phy mode info through the struct member
  *             phy_11b，phy_11g，phy_11n，phy_lr in the wifi_sta_info_t struct.
  *             For example, phy_11b = 1 imply that sta support 802.11b mode
  *
  * @return
  *    - ESP_OK: succeed
  *    - ESP_ERR_WIFI_NOT_INIT: WiFi is not initialized by esp_wifi_init
  *    - ESP_ERR_INVALID_ARG: invalid argument
  *    - ESP_ERR_WIFI_MODE: WiFi mode is wrong
  *    - ESP_ERR_WIFI_CONN: WiFi internal error, the station/soft-AP control block is invalid
  */
esp_err_t esp_wifi_ap_get_sta_list(wifi_sta_list_t *sta);


/**
  * @brief     Set the WiFi API configuration storage type
  *
  * @attention 1. The default value is WIFI_STORAGE_FLASH
  *
  * @param     storage : storage type
  *
  * @return
  *   - ESP_OK: succeed
  *   - ESP_ERR_WIFI_NOT_INIT: WiFi is not initialized by esp_wifi_init
  *   - ESP_ERR_INVALID_ARG: invalid argument
  */
esp_err_t esp_wifi_set_storage(wifi_storage_t storage);

/**
  * @brief     Set auto connect
  *            The default value is true
  *
  * @param     en : true - enable auto connect / false - disable auto connect
  *
  * @return
  *    - ESP_OK: succeed
  *    - ESP_ERR_WIFI_NOT_INIT: WiFi is not initialized by esp_wifi_init
  *    - ESP_ERR_WIFI_MODE: WiFi internal error, the station/soft-AP control block is invalid
  *    - others: refer to error code in esp_err.h
  */
esp_err_t esp_wifi_set_auto_connect(bool en) __attribute__ ((deprecated));

/**
  * @brief     Get the auto connect flag
  *
  * @param[out] en  store current auto connect configuration
  *
  * @return
  *    - ESP_OK: succeed
  *    - ESP_ERR_WIFI_NOT_INIT: WiFi is not initialized by esp_wifi_init
  *    - ESP_ERR_INVALID_ARG: invalid argument
  */
esp_err_t esp_wifi_get_auto_connect(bool *en) __attribute__ ((deprecated));

/**
  * @brief     Set 802.11 Vendor-Specific Information Element
  *
  * @param     enable If true, specified IE is enabled. If false, specified IE is removed.
  * @param     type Information Element type. Determines the frame type to associate with the IE.
  * @param     idx  Index to set or clear. Each IE type can be associated with up to two elements (indices 0 & 1).
  * @param     vnd_ie Pointer to vendor specific element data. First 6 bytes should be a header with fields matching vendor_ie_data_t.
  *            If enable is false, this argument is ignored and can be NULL. Data does not need to remain valid after the function returns.
  *
  * @return
  *    - ESP_OK: succeed
  *    - ESP_ERR_WIFI_NOT_INIT: WiFi is not initialized by esp_wifi_init()
  *    - ESP_ERR_INVALID_ARG: Invalid argument, including if first byte of vnd_ie is not WIFI_VENDOR_IE_ELEMENT_ID (0xDD)
  *      or second byte is an invalid length.
  *    - ESP_ERR_NO_MEM: Out of memory
  */
esp_err_t esp_wifi_set_vendor_ie(bool enable, wifi_vendor_ie_type_t type, wifi_vendor_ie_id_t idx, const void *vnd_ie);

/**
  * @brief     Function signature for received Vendor-Specific Information Element callback.
  * @param     ctx Context argument, as passed to esp_wifi_set_vendor_ie_cb() when registering callback.
  * @param     type Information element type, based on frame type received.
  * @param     sa Source 802.11 address.
  * @param     vnd_ie Pointer to the vendor specific element data received.
  * @param     rssi Received signal strength indication.
  */
typedef void (*esp_vendor_ie_cb_t) (void *ctx, wifi_vendor_ie_type_t type, const uint8_t sa[6], const vendor_ie_data_t *vnd_ie, int rssi);

/**
  * @brief     Register Vendor-Specific Information Element monitoring callback.
  *
  * @param     cb   Callback function
  * @param     ctx  Context argument, passed to callback function.
  *
  * @return
  *    - ESP_OK: succeed
  *    - ESP_ERR_WIFI_NOT_INIT: WiFi is not initialized by esp_wifi_init
  */
esp_err_t esp_wifi_set_vendor_ie_cb(esp_vendor_ie_cb_t cb, void *ctx);

/**
  * @brief     Set maximum WiFi transmiting power
  *
  * @attention WiFi transmiting power is divided to six levels in phy init data.
  *            Level0 represents highest transmiting power and level5 represents lowest
  *            transmiting power. Packets of different rates are transmitted in
  *            different powers according to the configuration in phy init data.
  *            This API only sets maximum WiFi transmiting power. If this API is called,
  *            the transmiting power of every packet will be less than or equal to the
  *            value set by this API. If this API is not called, the value of maximum
  *            transmitting power set in phy_init_data.bin or menuconfig (depend on
  *            whether to use phy init data in partition or not) will be used. Default
  *            value is level0. Values passed in power are mapped to transmit power
  *            levels as follows:
  *            - [78, 127]: level0
  *            - [76, 77]: level1
  *            - [74, 75]: level2
  *            - [68, 73]: level3
  *            - [60, 67]: level4
  *            - [52, 59]: level5
  *            - [44, 51]: level5 - 2dBm
  *            - [34, 43]: level5 - 4.5dBm
  *            - [28, 33]: level5 - 6dBm
  *            - [20, 27]: level5 - 8dBm
  *            - [8, 19]: level5 - 11dBm
  *            - [-128, 7]: level5 - 14dBm
  *
  * @param     power  Maximum WiFi transmiting power.
  *
  * @return
  *    - ESP_OK: succeed
  *    - ESP_ERR_WIFI_NOT_INIT: WiFi is not initialized by esp_wifi_init
  *    - ESP_ERR_WIFI_NOT_START: WiFi is not started by esp_wifi_start
  */
esp_err_t esp_wifi_set_max_tx_power(int8_t power);

/**
  * @brief     Get maximum WiFi transmiting power
  *
  * @attention This API gets maximum WiFi transmiting power. Values got
  *            from power are mapped to transmit power levels as follows:
  *            - 78: 19.5dBm
  *            - 76: 19dBm
  *            - 74: 18.5dBm
  *            - 68: 17dBm
  *            - 60: 15dBm
  *            - 52: 13dBm
  *            - 44: 11dBm
  *            - 34: 8.5dBm
  *            - 28: 7dBm
  *            - 20: 5dBm
  *            - 8:  2dBm
  *            - -4: -1dBm
  *
  * @param     power  Maximum WiFi transmiting power.
  *
  * @return
  *    - ESP_OK: succeed
  *    - ESP_ERR_WIFI_NOT_INIT: WiFi is not initialized by esp_wifi_init
  *    - ESP_ERR_WIFI_NOT_START: WiFi is not started by esp_wifi_start
  *    - ESP_ERR_INVALID_ARG: invalid argument
  */
esp_err_t esp_wifi_get_max_tx_power(int8_t *power);

/**
  * @brief     Set mask to enable or disable some WiFi events
  *
  * @attention 1. Mask can be created by logical OR of various WIFI_EVENT_MASK_ constants.
  *               Events which have corresponding bit set in the mask will not be delivered to the system event handler.
  * @attention 2. Default WiFi event mask is WIFI_EVENT_MASK_AP_PROBEREQRECVED.
  * @attention 3. There may be lots of stations sending probe request data around.
  *               Don't unmask this event unless you need to receive probe request data.
  *
  * @param     mask  WiFi event mask.
  *
  * @return
  *    - ESP_OK: succeed
  *    - ESP_ERR_WIFI_NOT_INIT: WiFi is not initialized by esp_wifi_init
  */
esp_err_t esp_wifi_set_event_mask(uint32_t mask);

/**
  * @brief     Get mask of WiFi events
  *
  * @param     mask  WiFi event mask.
  *
  * @return
  *    - ESP_OK: succeed
  *    - ESP_ERR_WIFI_NOT_INIT: WiFi is not initialized by esp_wifi_init
  *    - ESP_ERR_WIFI_ARG: invalid argument
  */
esp_err_t esp_wifi_get_event_mask(uint32_t *mask);

/**
  * @brief     Send user-define 802.11 packets.
  *
  * @attention 1. Packet has to be the whole 802.11 packet, does not include the FCS.
  *               The length of the packet has to be longer than the minimum length
  *               of the header of 802.11 packet which is 24 bytes, and less than 1400 bytes.
  * @attention 2. Duration area is invalid for user, it will be filled in SDK.
  * @attention 3. The rate of sending packet is same as the management packet which
  *               is the same as the system rate of sending packets.
  * @attention 4. Only after the previous packet was sent, entered the sent callback,
  *               the next packet is allowed to send. Otherwise, wifi_send_pkt_freedom
  *               will return fail.
  *
  * @param     uint8 *buf : pointer of packet
  * @param     uint16 len : packet length
  * @param     bool sys_seq : follow the system's 802.11 packets sequence number or not,
  *                           if it is true, the sequence number will be increased 1 every
  *                           time a packet sent.
  *
  * @return    ESP_OK, succeed;
  * @return    ESP_FAIL, fail.
  */
esp_err_t esp_wifi_send_pkt_freedom(uint8_t *buf, int32_t len, bool sys_seq);

#ifdef __cplusplus
}
#endif

#endif /* __ESP_WIFI_H__ */
