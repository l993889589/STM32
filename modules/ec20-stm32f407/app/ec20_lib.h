
#ifndef __EC20_LIB_H_
#define __EC20_LIB_H_


#include "main.h"



#define RX_BUFF_SIZE 128

#define EC20_EN 1

typedef struct
{
    unsigned int year;
    unsigned int month;
    unsigned int day;
    unsigned int hour;
    unsigned int minute;
    unsigned int second;
		unsigned int ms;
	
} ec20_time;


typedef struct
{
    char imei[16];   //15位的GSN   屁屁加0
    char state;

} device_imei_info;


typedef struct
{

    char iccid[21];  //20位的ICCID  屁屁加个\0
    char state;

} device_iccid_info;


typedef struct{
	
	device_iccid_info iccid;
	device_imei_info	imei;
	
}ec20_info;



typedef enum{

	EVENT_AT,
	EVENT_SIM,
	EVENT_REG,
	EVENT_CALL
	
}network_envet_id;


typedef struct{

	void(*at_ready_callback)(uint8_t state);
	uint8_t at_record_state;

	void(*sim_ready_callback)(uint8_t state); //检测到卡的回调
	uint8_t sim_record_state;
	
	void(*reg_ready_callback)(uint8_t state);
	uint8_t reg_record_state;
	
	void(*call_ready_callback)(ec20_time* time);
	uint8_t call_record_state;

}network_envet; //回调事件

/*at指令返回记录状态*/
typedef struct{

	uint8_t at_state;
	uint8_t sim_state;
	uint8_t reg_state;
	uint8_t call_state;
	
}network_check_state;


typedef struct{
	
	device_iccid_info iccid;
	device_imei_info	imei;
	
	uint8_t csq;
	
	network_check_state check_state;

	network_envet event;
	
	volatile uint32_t tick;
	
	volatile uint8_t task_call;  //查询标志位
	
}ec20_network;



#define EC20_PUB_MAX		 512 //方法内栈的消耗量是这个的两倍
#define EC20_TOPIC_MAX  64

#define MQTT_MON_IP_MAX			150
#define MQTT_MON_CID_MAX		150
#define MQTT_MON_UNAME_MAX	150
#define MQTT_MON_PSD_MAX		300
#define MQTT_MON_IP_MAX			150
#define MQTT_MON_CID_MAX		150
#define MQTT_MON_UNAME_MAX	150

#define TOPIC_MAX 100
#define TOPIC_NUM 10

typedef struct{
	
	uint8_t ip[ MQTT_MON_IP_MAX+1];
	uint16_t port;
	uint8_t clientid[MQTT_MON_CID_MAX+1];
	uint8_t username[MQTT_MON_UNAME_MAX+1];
	uint8_t password[ MQTT_MON_PSD_MAX+1];
	uint8_t channel;	
	
}ec20_mqtt_param;

typedef struct {

	uint8_t con_state;
	
}mqtt_state;


typedef  struct{

	void (*msgcb)(uint8_t* topic,uint8_t *data,int len);
	void (*concb)(uint8_t state);
	
	uint8_t concb_state; //通知过的状态

}mqtt_callback;

typedef  struct{

	char topic[TOPIC_MAX+1];
	
}mqtt_topic;

typedef struct{

	mqtt_topic topics[TOPIC_NUM ];
	
}mqtt_topic_set;


typedef struct{
	
	ec20_mqtt_param param;
	
	mqtt_topic_set topic;
	
  uint8_t  task_call; //触发了重连任务
	
	uint8_t  check_call;
	
	uint8_t  new_state;//tcp链接
	uint8_t  con_state;//mqtt链接
	uint8_t  con_task;//目标链接状态
	int task_tick;
	
	mqtt_state state;

	mqtt_callback callback;

	
}ec20_mqtt_handle;





/*******************************************ec20AT指令库函数*******************************************/

uint8_t ec20_at_test(void);
uint8_t ec20_at_ate(void);
uint8_t ec20_at_version_query(void);
uint8_t ec20_at_is_simready(void);
uint8_t ec20_at_is_reg(void);
uint8_t ec20_at_pdp_enable(void);
uint8_t ec20_at_pdp_disable(void);
int ec20_get_at_devicetime(ec20_time *temp);
uint8_t ec20_at_get_iccid(device_iccid_info *iccid);
uint8_t ec20_at_get_imei(device_imei_info *imei);
uint8_t ec20_at_connect_onenet_version_config(uint8_t client_idx,uint8_t vsn); 
uint8_t ec20_at_recv_len_enable(uint8_t channel,uint8_t state);
uint8_t ec20_at_tcp_open(uint8_t  id,uint8_t *ip,uint16_t  port);
uint8_t ec20_at_tcp_close(char id);
uint8_t ec20_at_mqtt_con(uint8_t channel,uint8_t *clientid,char *username,char *password);
uint8_t ec20_at_mqtt_close(uint8_t channel);
uint8_t ec20_at_mqtt_get_state(uint8_t channel);
uint8_t ec20_at_mqtt_sub(uint8_t channel,uint8_t * topic,uint8_t qos);
uint8_t ec20_at_mqtt_pub(uint8_t channel,uint8_t * topic,uint8_t *msg,int length);
uint8_t ec20_at_get_ntp_server_time(char *server,ec20_time* temp);


/*******************************************ec20network函数*******************************************/

void		ec20_lib_network_init(ec20_network* _network);
void 		ec20_lib_network_tick(ec20_network* _network,uint8_t tick);
void 		ec20_lib_network_tick_update(ec20_network* _network);
void 		ec20_lib_network_clear(ec20_network* _network);
void 		ec20_lib_network_event_add(ec20_network *_network,void(*at_event_cb)(uint8_t state),void(*sim_event_cb)(uint8_t state),void(*reg_event_cb)(uint8_t state),void(*call_event_cb)(ec20_time* time));
uint8_t ec20_lib_network_get_at_state(ec20_network* _network);
uint8_t ec20_lib_network_get_sim_state(ec20_network* _network);
uint8_t ec20_lib_network_get_reg_state(ec20_network* _network);
uint8_t ec20_lib_network_get_call_state(ec20_network* _network);
uint8_t ec20_lib_network_get_imei(ec20_network* _network,device_imei_info* imei);
void 		ec20_lib_network_process(ec20_network* _network);


/*******************************************ec20  mqtt函数*******************************************/

static void ec20_lib_mqtt_freash_tick(ec20_mqtt_handle * mqhandle,int tick);
void 				ec20_lib_mqtt_tick(ec20_mqtt_handle* mqtt_handle,uint32_t tick);
uint8_t 		ec20_lib_mqtt_init(ec20_mqtt_handle* mqtt_handle,uint8_t channel,uint8_t *ip,uint16_t port,uint8_t*clientid,uint8_t*username,uint8_t*password);
void 				ec20_lib_mqtt_process(ec20_mqtt_handle * mqtt_handle);
uint8_t 		ec20_lib_mqtt_regcb(ec20_mqtt_handle * mqtt_handle,void (*msgcb)(uint8_t* topic,uint8_t *data,int len),void (*concb)(uint8_t state));
uint8_t 		ec20_lib_mqtt_close_wait(ec20_mqtt_handle * mqtt_handle,uint8_t channel);
uint8_t 		ec20_lib_mqtt_control(ec20_mqtt_handle * mqhandle,uint8_t state);
uint8_t 		ec20_lib_mqtt_yeild(ec20_mqtt_handle * mqhandle,char *msg, int length);
void ec20_lib_mqtt_process_aliauth(ec20_mqtt_handle * mqtt_handle);


uint8_t ec20_lib_mqtt_topic_set(ec20_mqtt_handle * mqtt_handle,uint8_t id,char *msg,uint8_t len);
char* ec20_lib_mqtt_topic_get(ec20_mqtt_handle * mqtt_handle,uint8_t id);


void  byte_to_hexstr(const unsigned char* source, char* dest, int sourceLen);




#endif




