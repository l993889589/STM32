#ifndef __LEDUO_H_
#define __LEDUO_H_


#include "main.h"




typedef struct
{
    unsigned int year;
    unsigned int month;
    unsigned int day;
    unsigned int hour;
    unsigned int minute;
    unsigned int second;
		unsigned int ms;
	
} leduo_time;




uint16_t CRC16_Modbus(uint8_t *_pBuf, uint16_t _usLen);

void  int_byte_to_hexstr(const unsigned char* source, char* dest, int sourceLen) ;

void uint8_to_float(uint8_t *data,float *f);

void log_hex_msg(const char*msg,uint8_t*data,int len);
//uint8_t find_parcel(uint8_t *data, char *left, char *right, uint8_t *out);
//uint32_t find_cnt_state_to_end(uint8_t *data, int length, char sign, int start, uint8_t *out,uint32_t *out_cnt);
//void data_left(uint8_t *data, int start, int leftsize);
//uint32_t get_data_by_sign(const char *data, uint32_t length, char sign, uint32_t start, char *out) ;
//int get_data_between_commas( char *str, char *out);



// void leduo_utc_to_beijing(leduo_time *time);
// void  byte_to_hexstr(const unsigned char* source, char* dest, int sourceLen);
// uint32_t find_pos(char *msg,char sign,int cnt);
#endif


