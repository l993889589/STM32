#ifndef APP_MAIN_H
#define APP_MAIN_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define ADDR_COUNT  14U
#define BUFFER_SIZE 256U

typedef struct
{
    uint8_t record_state;
    uint8_t updata_state;
    uint8_t (*dwin_event_callback)(uint8_t state);
} dwin_envent_t;

typedef struct
{
    volatile uint8_t call;
    volatile uint8_t state;
    volatile uint16_t tick;
    dwin_envent_t dwin_event;
} ping_t;

typedef struct
{
    uint8_t init_state;
    uint8_t missing_count;
    uint8_t confirm_count;
    uint16_t missing_addrs[ADDR_COUNT];
    uint16_t confirm_addrs[ADDR_COUNT];
} dwin_info_t;

typedef struct
{
    uint16_t initial_addr;
    uint16_t corresponding_addr;
    const unsigned char *normal_data;
    size_t normal_length;
    const unsigned char *abnormal_data;
    size_t abnormal_length;
} address_mapping_t;

typedef struct
{
    float temp;
    float humi;
    float cpu_temp;
    float gpu_temp;
} environmental_param_t;

typedef struct
{
    ping_t ping;
    dwin_info_t dwin_info;
    uint8_t connect_state;
    environmental_param_t environmental_param;
} server_t;

void init_mutex(void);
void acquire_mutex(void);
void release_mutex(void);
uint8_t get_init_state(void);
bool is_crc_enabled(void);
void app_server_init(void);
void app_server_tick(void);
void app_server_refresh(void);
void check_server_ping(void);
uint8_t get_connect_state(void);
void set_connect_state(uint8_t state);
void set_crc_enabled(bool enabled);
void set_init_state(uint8_t state);
void connect_msg_dispaly(uint8_t state);
bool send_page_switch_command(uint8_t page);
bool send_page_switch_command_ex(uint8_t page);
void update_environmental_params(float temp, float humi);
void msg_analysis(unsigned char *message, int messagelen);
void msg_analysis_crc(unsigned char *message, int messagelen);
void usb_parse_crc(unsigned char *message, int messagelen);
bool send_alarm_color_packet(uint16_t address, uint16_t color_data);
bool send_packet_with_crc(uint16_t addr, uint8_t *data, uint8_t len, bool enable_crc);
bool send_alarm_color_packet_with_offset(uint16_t initial_addr,
                                         uint16_t color_data,
                                         uint8_t event_type);
void build_packet_with_crc(uint16_t addr,
                           const uint8_t *data,
                           size_t len,
                           uint8_t *outdata,
                           bool enable_crc);
uint8_t BuildPacketWithID(uint16_t addr,
                          uint8_t device_id,
                          const uint8_t *data,
                          size_t len,
                          uint8_t *outdata);
uint8_t build_packet_with_crc1(uint16_t addr,
                               uint8_t device_id,
                               const uint8_t *data,
                               size_t len,
                               uint8_t *outdata);
void handle_temperature_conditions(void);
void ResetDiskColorAndEvent(void);

#endif
