/**
 * @file app_main.h
 * @brief CHPM host protocol state and display-facing application API.
 */

#ifndef APP_MAIN_H
#define APP_MAIN_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define ADDR_COUNT  14U
#define BUFFER_SIZE 256U

/** @brief One host-reported DWIN event state. */
typedef struct
{
    uint8_t record_state;
    uint8_t updata_state;
    uint8_t (*dwin_event_callback)(uint8_t state);
} dwin_envent_t;

/** @brief Host heartbeat state. */
typedef struct
{
    volatile uint8_t call;
    volatile uint8_t state;
    volatile uint16_t tick;
    dwin_envent_t dwin_event;
} ping_t;

/** @brief Host initialization and address-discovery state. */
typedef struct
{
    uint8_t init_state;
    uint8_t missing_count;
    uint8_t confirm_count;
    uint16_t missing_addrs[ADDR_COUNT];
    uint16_t confirm_addrs[ADDR_COUNT];
} dwin_info_t;

/** @brief Mapping from host event address to DWIN presentation address. */
typedef struct
{
    uint16_t initial_addr;
    uint16_t corresponding_addr;
    const unsigned char *normal_data;
    size_t normal_length;
    const unsigned char *abnormal_data;
    size_t abnormal_length;
} address_mapping_t;

/** @brief Cached environmental values shared with host replies. */
typedef struct
{
    float temp;
    float humi;
    float cpu_temp;
    float gpu_temp;
} environmental_param_t;

/** @brief Complete application protocol state. */
typedef struct
{
    ping_t ping;
    dwin_info_t dwin_info;
    uint8_t connect_state;
    environmental_param_t environmental_param;
} server_t;

/** @brief Compatibility no-op; mutexes are created during ThreadX startup. */
void init_mutex(void);

/** @brief Lock shared application protocol state. */
void acquire_mutex(void);

/** @brief Unlock shared application protocol state. */
void release_mutex(void);

/** @brief Return whether host initialization completed. */
uint8_t get_init_state(void);

/** @brief Return whether incoming private frames require CRC validation. */
bool is_crc_enabled(void);

/** @brief Initialize application protocol state. */
void app_server_init(void);

/** @brief Advance the millisecond heartbeat timeout state. */
void app_server_tick(void);

/** @brief Mark the host heartbeat as observed. */
void app_server_refresh(void);

/** @brief Apply a pending heartbeat connectivity decision. */
void check_server_ping(void);

/** @brief Return current host connectivity state. */
uint8_t get_connect_state(void);

/** @brief Override current host connectivity state. */
void set_connect_state(uint8_t state);

/** @brief Enable or disable incoming private-frame CRC validation. */
void set_crc_enabled(bool enabled);

/** @brief Set host initialization completion state. */
void set_init_state(uint8_t state);

/** @brief Submit the host connectivity indicator as a latest DWIN value. */
void connect_msg_dispaly(uint8_t state);

/** @brief Submit an ordered DWIN page switch. */
bool send_page_switch_command(uint8_t page);

/** @brief Compatibility alias for send_page_switch_command(). */
bool send_page_switch_command_ex(uint8_t page);

/** @brief Update cached environment values. */
void update_environmental_params(float temp, float humi);

/** @brief Feed bytes received from the DWIN side into the frame parser. */
void msg_analysis(unsigned char *message, int messagelen);

/** @brief Compatibility entry for CRC-enabled DWIN-side parsing. */
void msg_analysis_crc(unsigned char *message, int messagelen);

/** @brief Feed bytes received from USB CDC into the host frame parser. */
void usb_parse_crc(unsigned char *message, int messagelen);

/** @brief Submit one ordered DWIN color write. */
bool send_alarm_color_packet(uint16_t address, uint16_t color_data);

/** @brief Map a host event address and submit its DWIN color write. */
bool send_alarm_color_packet_with_offset(uint16_t initial_addr,
                                         uint16_t color_data,
                                         uint8_t event_type);

/** @brief Re-evaluate CPU, GPU, and DS18B20 alarm state. */
void handle_temperature_conditions(void);

/** @brief Restore default disk and temperature display colors. */
void ResetDiskColorAndEvent(void);

#endif /* APP_MAIN_H */
