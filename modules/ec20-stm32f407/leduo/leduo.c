#include "main.h"
#include "bsp_usart.h"
#include "leduo.h"

#include "stdlib.h"
#include "stdio.h"
#include "string.h"

#include "log.h"

    // CRC 高位字节值表
static const uint8_t s_CRCHi[] = {
    0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41, 0x01, 0xC0,
    0x80, 0x41, 0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41,
    0x00, 0xC1, 0x81, 0x40, 0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0,
    0x80, 0x41, 0x01, 0xC0, 0x80, 0x41, 0x00, 0xC1, 0x81, 0x40,
    0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41, 0x00, 0xC1,
    0x81, 0x40, 0x01, 0xC0, 0x80, 0x41, 0x01, 0xC0, 0x80, 0x41,
    0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41, 0x00, 0xC1,
    0x81, 0x40, 0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41,
    0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41, 0x01, 0xC0,
    0x80, 0x41, 0x00, 0xC1, 0x81, 0x40, 0x00, 0xC1, 0x81, 0x40,
    0x01, 0xC0, 0x80, 0x41, 0x01, 0xC0, 0x80, 0x41, 0x00, 0xC1,
    0x81, 0x40, 0x01, 0xC0, 0x80, 0x41, 0x00, 0xC1, 0x81, 0x40,
    0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41, 0x01, 0xC0,
    0x80, 0x41, 0x00, 0xC1, 0x81, 0x40, 0x00, 0xC1, 0x81, 0x40,
    0x01, 0xC0, 0x80, 0x41, 0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0,
    0x80, 0x41, 0x01, 0xC0, 0x80, 0x41, 0x00, 0xC1, 0x81, 0x40,
    0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41, 0x01, 0xC0,
    0x80, 0x41, 0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41,
    0x00, 0xC1, 0x81, 0x40, 0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0,
    0x80, 0x41, 0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41,
    0x01, 0xC0, 0x80, 0x41, 0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0,
    0x80, 0x41, 0x00, 0xC1, 0x81, 0x40, 0x00, 0xC1, 0x81, 0x40,
    0x01, 0xC0, 0x80, 0x41, 0x01, 0xC0, 0x80, 0x41, 0x00, 0xC1,
    0x81, 0x40, 0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41,
    0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41, 0x01, 0xC0,
    0x80, 0x41, 0x00, 0xC1, 0x81, 0x40
} ;
// CRC 低位字节值表
const uint8_t s_CRCLo[] = {
	0x00, 0xC0, 0xC1, 0x01, 0xC3, 0x03, 0x02, 0xC2, 0xC6, 0x06,
	0x07, 0xC7, 0x05, 0xC5, 0xC4, 0x04, 0xCC, 0x0C, 0x0D, 0xCD,
	0x0F, 0xCF, 0xCE, 0x0E, 0x0A, 0xCA, 0xCB, 0x0B, 0xC9, 0x09,
	0x08, 0xC8, 0xD8, 0x18, 0x19, 0xD9, 0x1B, 0xDB, 0xDA, 0x1A,
	0x1E, 0xDE, 0xDF, 0x1F, 0xDD, 0x1D, 0x1C, 0xDC, 0x14, 0xD4,
	0xD5, 0x15, 0xD7, 0x17, 0x16, 0xD6, 0xD2, 0x12, 0x13, 0xD3,
	0x11, 0xD1, 0xD0, 0x10, 0xF0, 0x30, 0x31, 0xF1, 0x33, 0xF3,
	0xF2, 0x32, 0x36, 0xF6, 0xF7, 0x37, 0xF5, 0x35, 0x34, 0xF4,
	0x3C, 0xFC, 0xFD, 0x3D, 0xFF, 0x3F, 0x3E, 0xFE, 0xFA, 0x3A,
	0x3B, 0xFB, 0x39, 0xF9, 0xF8, 0x38, 0x28, 0xE8, 0xE9, 0x29,
	0xEB, 0x2B, 0x2A, 0xEA, 0xEE, 0x2E, 0x2F, 0xEF, 0x2D, 0xED,
	0xEC, 0x2C, 0xE4, 0x24, 0x25, 0xE5, 0x27, 0xE7, 0xE6, 0x26,
	0x22, 0xE2, 0xE3, 0x23, 0xE1, 0x21, 0x20, 0xE0, 0xA0, 0x60,
	0x61, 0xA1, 0x63, 0xA3, 0xA2, 0x62, 0x66, 0xA6, 0xA7, 0x67,
	0xA5, 0x65, 0x64, 0xA4, 0x6C, 0xAC, 0xAD, 0x6D, 0xAF, 0x6F,
	0x6E, 0xAE, 0xAA, 0x6A, 0x6B, 0xAB, 0x69, 0xA9, 0xA8, 0x68,
	0x78, 0xB8, 0xB9, 0x79, 0xBB, 0x7B, 0x7A, 0xBA, 0xBE, 0x7E,
	0x7F, 0xBF, 0x7D, 0xBD, 0xBC, 0x7C, 0xB4, 0x74, 0x75, 0xB5,
	0x77, 0xB7, 0xB6, 0x76, 0x72, 0xB2, 0xB3, 0x73, 0xB1, 0x71,
	0x70, 0xB0, 0x50, 0x90, 0x91, 0x51, 0x93, 0x53, 0x52, 0x92,
	0x96, 0x56, 0x57, 0x97, 0x55, 0x95, 0x94, 0x54, 0x9C, 0x5C,
	0x5D, 0x9D, 0x5F, 0x9F, 0x9E, 0x5E, 0x5A, 0x9A, 0x9B, 0x5B,
	0x99, 0x59, 0x58, 0x98, 0x88, 0x48, 0x49, 0x89, 0x4B, 0x8B,
	0x8A, 0x4A, 0x4E, 0x8E, 0x8F, 0x4F, 0x8D, 0x4D, 0x4C, 0x8C,
	0x44, 0x84, 0x85, 0x45, 0x87, 0x47, 0x46, 0x86, 0x82, 0x42,
	0x43, 0x83, 0x41, 0x81, 0x80, 0x40
};


/*
*********************************************************************************************************
*	函 数 名: be_buff_to_uint16
*	功能说明: 将2字节数组(大端Big Endian次序，高字节在前)转换为16位整数
*	形    参: _pBuf : 数组
*	返 回 值: 16位整数值
*
*   大端(Big Endian)与小端(Little Endian)
*********************************************************************************************************
*/
uint16_t be_buff_to_uint16(uint8_t *buff)
{
    return (((uint16_t)buff[0] << 8) | buff[1]);
}

/*
*********************************************************************************************************
*	函 数 名: le_buff_to_uint16
*	功能说明: 将2字节数组(小端Little Endian，低字节在前)转换为16位整数
*	形    参: _pBuf : 数组
*	返 回 值: 16位整数值
*********************************************************************************************************
*/
uint16_t le_buff_to_uint16(uint8_t *buff)
{
    return (((uint16_t)buff[1] << 8) | buff[0]);
}




void  int_byte_to_hexstr(const unsigned char* source, char* dest, int sourceLen)  
{  
    int i;  
    unsigned char highByte, lowByte;  
  
    for (i = 0; i < sourceLen; i++)  
    {  
        highByte = source[i] >> 4;  
        lowByte = source[i] & 0x0f ;  
  
    if (highByte > 9)  
		{	
            dest[i * 2] = highByte + 0x37;
		}
        else  
		{
			dest[i * 2] = highByte + '0';  
		}		
        if (lowByte > 9)  
		{
			dest[i * 2 + 1] = lowByte + 0x37;  
		}        
		else  
		{    
			dest[i * 2 + 1] = lowByte + '0';
		}
    }  
    return ;  
} 







void uint8_to_float(uint8_t *data,float *f){
		for(int i=0;i<4;i++){
			((uint8_t*)f)[i]=data[3-i];
		}
}


void log_hex_msg(const char*msg,uint8_t*data,int len){
	
	
	if(len<64){
			char convert_buff[128]={0};
			
			if(len){
					int_byte_to_hexstr(data,convert_buff,len);
					log("%s:%d,[%s]",msg,len,convert_buff);
			}
			else{
					log("%s:%d,[null]",msg,len);
			}		
	}

}

///**
// * @brief 找到特定的前后包裹的数据
// *
// * @param data
// * @param left
// * @param right
// * @param out
// * @return uint8_t
// */
//uint8_t find_parcel(uint8_t *data, char *left, char *right, uint8_t *out)
//{

//	char *p = 0, *e = 0;
//	if ((p = strstr((char *)data, left)) && (e = strstr(p, right)))
//	{

//		memcpy(out, p + (strlen(left)), e - p - strlen(left));

//		return 1;
//	}
//	return 0;
//}



///**
// * @brief 根据指定位置的标志，开始获取数据，直到再次遇到标志或者尽头为止。
// * "+ZMQRCV: 1,/moekon/substopic,1,0,0,56,12341234567890123412345678901234123456789012341234567890"
// * sign->','  start->5 -----> 56
// * sign->','  start->6 -----> 12341234567890123412345678901234123456789012341234567890
// *
// * @param data
// * @param length
// * @param sign
// * @param start
// * @param out
// * @return uint32_t 实际截取成功的长度，长度为0时，代表失败
// */

///* 定义一个函数，用于从一个字节数组中查找从第start个sign字符开始到下一个sign字符结束的子数组，并将其复制到out数组中，同时返回子数组的长度和sign字符的个数*/
//uint32_t find_cnt_state_to_end(uint8_t *data, int length, char sign, int start, uint8_t *out,uint32_t *out_cnt)
//{

//	int find_cnt = 0; // 用于记录已经找到的sign字符的个数
//	int state = 0; // 用于标记是否已经开始复制子数组
//	int out_offset = 0; // 用于记录out数组的偏移量

//	for (int i = 0; i < length; i++) // 遍历data数组
//		{
//			if (state) // 如果已经开始复制子数组
//			{
//				if (data[i] == sign){ // 如果遇到了sign字符，说明子数组结束
//					(*out_cnt)++; // 增加sign字符的个数
//					return out_offset; // 返回子数组的长度
//				}
//				
//				out[out_offset] = data[i]; // 将data[i]复制到out[out_offset]
//				out_offset++; // 增加out数组的偏移量
//			}
//			else // 如果还没有开始复制子数组
//			{
//				if (data[i] == sign) // 如果遇到了sign字符
//				{
//					find_cnt++; // 增加sign字符的个数
//					(*out_cnt)=i; // 记录当前sign字符在data数组中的位置

//					if (find_cnt == start) // 如果已经找到了第start个sign字符，说明子数组开始
//					{
//						state = 1; // 标记开始复制子数组
//					}
//				}
//			}
//		}
//		
//		if (state) // 如果已经开始复制子数组，但是没有遇到下一个sign字符，说明子数组一直到data数组的末尾
//		{
//			(*out_cnt)++; // 增加sign字符的个数
//			return out_offset; // 返回子数组的长度
//		}
//		return 0; // 如果没有找到任何符合条件的子数组，返回0
//}





//void data_left(uint8_t *data, int start, int leftsize)
//{

//	for (int i = 0; i < leftsize; i++)
//	{
//		// debug_printf("%d:[%c] %d:[%c]",i,data[i],i+start,data[i+start]);
//		data[i] = data[i + start];
//		// debug_printf("data[%d]->[%c]",i,data[i]);
//	}
//	data[leftsize] = 0;
//	// debug_printf("data[0]->[%c],data[1]->[%c][%c][%c]",data[0],data[1],data[2],data[3]);
//}


//// 根据指定位置的标志，开始获取数据，直到再次遇到标志或者尽头为止。
//// "+ZMQRCV: 1,/moekon/substopic,1,0,0,56,12341234567890123412345678901234123456789012341234567890"
//// sign->','  start->5 -----> 56
//// sign->','  start->6 -----> 12341234567890123412345678901234123456789012341234567890
////
//// @param data 输入的数据字符串
//// @param length 输入的数据字符串的长度
//// @param sign 指定的标志字符
//// @param start 指定的开始位置（从1开始计数）
//// @param out 输出的结果字符串（需要提前分配足够的空间）
//// @return uint32_t 实际截取成功的长度，长度为0时，代表失败
//uint32_t get_data_by_sign(const char *data, uint32_t length, char sign, uint32_t start, char *out) {
//    // 参数检查
//    if (data == NULL || length == 0 || out == NULL || start == 0) {
//        return 0;
//    }
//    // 定位到开始位置
//    uint32_t i = 0;
//    uint32_t count = 0; // 记录遇到的标志字符的个数
//    while (i < length && count < start) {
//        if (data[i] == sign) {
//            count++;
//        }
//        i++;
//    }
//    // 如果没有找到开始位置，返回失败
//    if (count < start) {
//        return 0;
//    }
//    // 复制数据，直到遇到标志字符或者尽头为止
//    uint32_t j = 0;
//    while (i < length && data[i] != sign) {
//        out[j] = data[i];
//        i++;
//        j++;
//    }
//    // 在输出字符串末尾添加'\0'
//    out[j] = '\0';
//    // 返回截取成功的长度
//    return j;
//}



//// 把AT+ZMQSUB=1,3201FFFF/push,0 两个逗号中间的数据截取出来
//// @param str 输入的字符串
//// @param out 输出的结果字符串（需要提前分配足够的空间）
//// @return int 截取成功的长度，长度为0时，代表失败
//int get_data_between_commas( char *str, char *out) {
//    // 参数检查
//    if (str == NULL || out == NULL) {
//        return 0;
//    }
//    // 复制一份输入字符串，因为strtok会修改原字符串
//    char temp[100];
////		memcpy(linhao,leduo+10,7);
//    strcpy(temp, str);
//    // 定义分隔符
//    char delim[] = ",";
//    // 使用strtok分割字符串，每次返回一个子串
//    char *token = strtok(temp, delim);
//    int count = 0; // 记录遇到的子串的个数
//    while (token != NULL) {
//        // 如果是第二个子串，就是我们要找的数据
//        if (count == 1) {
//            // 复制子串到输出字符串中
//            strcpy(out, token);
//            // 返回截取成功的长度
//            return strlen(token);
//        }
//        // 计数器加一
//        count++;
//        // 继续分割字符串，直到返回NULL为止
//        token = strtok(NULL, delim);
//    }
//    // 如果没有找到数据，返回失败
//    return 0;
//}





///**
// * @brief utc时间转换成北京时间
// * 
// * @param time 
// */
//void leduo_utc_to_beijing(leduo_time *time)
//{
//    unsigned char days = 0;

//    if (time->month == 1 || time->month == 3 || time->month == 5 || time->month == 7 || time->month == 8 || time->month == 10 || time->month == 12)
//    {
//        days = 31;
//    }
//    else if (time->month == 4 || time->month == 6 || time->month == 9 || time->month == 11)
//    {
//        days = 30;
//    }
//    else if (time->month == 2)
//    {
//        if ((time->year % 400 == 0) || ((time->year % 4 == 0) && (time->year % 100 != 0)))
//        {
//            days = 29;
//        }
//        else
//        {
//            days = 28;
//        }
//    }
//    time->hour += 8;

//    if (time->hour >= 24)
//    {
//        time->hour -= 24;
//        time->day++;
//        if (time->day > days)
//        {
//            time->day = 1;
//            time->month++;
//            if (time->month > 12)
//            {
//                time->year++;
//            }
//        }
//    }
//}



///**
// * @brief byte转hex
// * 
// * @param source 
// * @param dest 
// * @param sourceLen 
// */
//void  byte_to_hexstr(const unsigned char* source, char* dest, int sourceLen)  
//{  
//    int i;  
//    unsigned char highByte, lowByte;  
//  
//    for (i = 0; i < sourceLen; i++)  
//    {  
//        highByte = source[i] >> 4;  
//        lowByte = source[i] & 0x0f ;  
//  
//    if (highByte > 9)  
//		{	
//            dest[i * 2] = highByte + 0x37;
//		}
//        else  
//		{
//			dest[i * 2] = highByte + '0';  
//		}		
//        if (lowByte > 9)  
//		{
//			dest[i * 2 + 1] = lowByte + 0x37;  
//		}        
//		else  
//		{    
//			dest[i * 2 + 1] = lowByte + '0';
//		}
//    }  
//    return ;  
//}  



///**
// * @brief 找点
// * 
// * @param msg 
// * @param sign 
// * @param cnt 
// * @return uint32_t 
// */
//uint32_t find_pos(char *msg,char sign,int cnt){
//	
//		int length=strlen(msg);
//		int n=0;
//		for(int i=0;i<length;i++){
//			
//				if(msg[i]==sign){
//					n++;
//				}
//				if(n==cnt){
//					return i;
//				}
//		}
//		return 0;
//}



/*
*********************************************************************************************************
*	函 数 名: CRC16_Modbus
*	功能说明: 计算CRC。 用于Modbus协议。
*	形    参: _pBuf : 参与校验的数据
*			  _usLen : 数据长度
*	返 回 值: 16位整数值。 对于Modbus ，此结果高字节先传送，低字节后传送。
*
*   所有可能的CRC值都被预装在两个数组当中，当计算报文内容时可以简单的索引即可；
*   一个数组包含有16位CRC域的所有256个可能的高位字节，另一个数组含有低位字节的值；
*   这种索引访问CRC的方式提供了比对报文缓冲区的每一个新字符都计算新的CRC更快的方法；
*
*  注意：此程序内部执行高/低CRC字节的交换。此函数返回的是已经经过交换的CRC值；也就是说，该函数的返回值可以直接放置
*        于报文用于发送；
*********************************************************************************************************
*/
uint16_t CRC16_Modbus(uint8_t *_pBuf, uint16_t _usLen)
{
	uint8_t ucCRCHi = 0xFF; /* 高CRC字节初始化 */
	uint8_t ucCRCLo = 0xFF; /* 低CRC 字节初始化 */
	uint16_t usIndex;  /* CRC循环中的索引 */

    while (_usLen--)
    {
		usIndex = ucCRCHi ^ *_pBuf++; /* 计算CRC */
		ucCRCHi = ucCRCLo ^ s_CRCHi[usIndex];
		ucCRCLo = s_CRCLo[usIndex];
    }
    return ((uint16_t)ucCRCHi << 8 | ucCRCLo);
}
