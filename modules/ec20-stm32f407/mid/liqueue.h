#ifndef __LIQUEUE_H_
#define __LIQUEUE_H_

#include "main.h"

typedef enum
{

	NO_OVERWRITE,  //数据保护
	CAN_OVERWRITE, //不保护数据，允许覆盖

} OVERLAY_TYPE;

typedef struct
{
	uint32_t node_max;
	OVERLAY_TYPE overlay_type;

} lq_config;

typedef struct
{
	uint8_t *pool;
	uint32_t pool_mem_size;
	uint32_t offset; //当前节点的偏移
	uint32_t head;
	uint32_t tail;
	uint32_t usage; //这样比实时计算运算量小
	unsigned char full;

} lq_pool;

typedef struct
{
	uint32_t head;
	uint32_t tail;

} lq_node;

typedef struct
{
	volatile int tick;
	uint32_t set_timeout;
	uint8_t enable;

} lq_timer;

typedef struct{
	
	volatile uint8_t   new_state;
	volatile uint8_t * block_buff;
	uint32_t buff_size;

	volatile uint32_t  index;

	volatile int  timetick;

	volatile uint8_t   timeout;

}lq_block;


typedef enum{
	
	MEM_QUEUE=0,
	MEM_BLOCK=1,
	
}MEM_MODE;

typedef struct
{

	lq_node * node_list;
	volatile  uint32_t  node_in;
	volatile  uint32_t  node_out;
	volatile  uint32_t  node_len;
	volatile  unsigned  char full;
	lq_block  block;
//	uint8_t buff[1024];
	volatile MEM_MODE  mem_mode;

} lq_node_list; //节点只负责存储信息，他只负责存储包的位置信息

typedef struct
{
	lq_pool pool;
	lq_node_list node_list;

} lq_mem_pool;

typedef struct
{
	
	lq_config cfg;
	lq_mem_pool mem;
	lq_timer timer;
	void (*genover)();

} lq_handle;

#define POOL_ARRAY_SIZE(N) (N)
#define NODE_ARRAY_SIZE(N) (sizeof(lq_node) * N)
	
uint8_t lq_mem_array_init(lq_handle *h, uint8_t *pool_mem, uint32_t mem_size, uint8_t *node_mem, uint32_t node_len);

uint32_t lq_mem_init(lq_handle *h, uint32_t mem_size, uint32_t node_len);

uint32_t lq_init(lq_handle *h, uint32_t node_max, OVERLAY_TYPE overlay_type);
uint32_t lq_add(lq_handle* h, uint8_t* data, uint32_t datalen);
uint32_t lq_settle(lq_handle* h);
uint32_t lq_push(lq_handle* h, uint8_t* buff, uint32_t buffsize);
uint32_t lq_pop(lq_handle *h, uint8_t *buff, uint32_t buffsize);



uint32_t lq_timer_init(lq_handle *h, uint32_t timeout);
uint32_t lq_read(lq_handle *h, uint8_t * buff,uint32_t buffsize,uint32_t timeout);
uint32_t lq_read_ex(lq_handle *h, uint8_t * buff,uint32_t buffsize,uint32_t timeout,void (*waittask)());
uint32_t lq_reg_cb(lq_handle *h,void (*overtask)());


void lq_tick(lq_handle *h,uint8_t tick);


#endif





