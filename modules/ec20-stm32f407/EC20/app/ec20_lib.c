/*
*********************************************************************************************************
*
*	模块名称 : ec20_lib文件
*	文件名称 : ec20_lib.c
*	版    本 : V1.0
*	说    明 : 使用了ec20_lib驱动文件,此文件用来给网络托管,mqtt托管提供基本的函数
*						 主要是根据ec20手册,完成at指令的基本操作
*						[2023-05-18 11:11:54.340]# RECV ASCII>[AT+QMTOPEN=0,"183.230.40.39",6002
*             
*           [2023-05-18 11:11:54.497]# RECV ASCII> recv:+QMTOPEN: 0,0
*
*			[2023-05-18 11:12:25.089]# RECV ASCII> recv:+QMTSTAT: 0,1  open以后如果一段时间没有建立mqtt连接，会被服务器踢开
*	
*	调用文件 :  gm331_lib.h/bsp_usart.h
*
*
*
*	修改记录 :
*		版本号  日期        	作者     			说明
*		V1.0    2023-05-08  	liam&leduo  
*		V1.1    2023-05-30    leduo 				新增了手动添加topic的功能
*
*********************************************************************************************************
*/


#include "main.h"
#include "string.h"
#include "stdio.h"
#include "stdlib.h"

#include "ec20_drv.h"
#include "ec20_lib.h"

#include "leduo.h"

#include "log.h"

/**
 * @brief utc时间转换成北京时间
 * 
 * @param time 
 */
static void utc_to_beijing(ec20_time *time)
{
    unsigned char days = 0;

    if (time->month == 1 || time->month == 3 || time->month == 5 || time->month == 7 || time->month == 8 || time->month == 10 || time->month == 12)
    {
        days = 31;
    }
    else if (time->month == 4 || time->month == 6 || time->month == 9 || time->month == 11)
    {
        days = 30;
    }
    else if (time->month == 2)
    {
        if ((time->year % 400 == 0) || ((time->year % 4 == 0) && (time->year % 100 != 0)))
        {
            days = 29;
        }
        else
        {
            days = 28;
        }
    }
    time->hour += 8;

    if (time->hour >= 24)
    {
        time->hour -= 24;
        time->day++;
        if (time->day > days)
        {
            time->day = 1;
            time->month++;
            if (time->month > 12)
            {
                time->year++;
            }
        }
    }
}




uint32_t find_pos(char *msg,char sign,int cnt){
	
		int length=strlen(msg);
		int n=0;
		for(int i=0;i<length;i++){
			
				if(msg[i]==sign){
					n++;
				}
				if(n==cnt){
					return i;
				}
		}
		return 0;
}


/**
 * @brief 检测ec20模块是否在位
 * 
 * @return uint8_t 
 */
uint8_t ec20_at_test(void){
	
    unsigned char read_buff[RX_BUFF_SIZE] = {0};

    char *write_buff = "AT\r\n";

    ec20_drv_write((uint8_t *)write_buff, strlen(write_buff));

        
    if ( ec20_drv_blockread(read_buff, RX_BUFF_SIZE, 1000))
    {
        if (strstr((char *)read_buff, "OK"))
        {

            return 1;
        }
    }

    return 0;	
}



/**
 * @brief 关闭回显
 * 
 * @return uint8_t 
 */
uint8_t ec20_at_ate(void)
{
	unsigned char read_buff[RX_BUFF_SIZE]={0};
	
	char *write_buff="ATE0\r\n";
	
	ec20_drv_write((uint8_t *)write_buff, strlen(write_buff));
	
	 if ( ec20_drv_blockread(read_buff, RX_BUFF_SIZE, 5000))
	 {
		    if (strstr((char *)read_buff, "OK"))
        {
						log("关闭回显\r\n");
            return 1;
        }
	 }
	 return 0;
}


/**
 * @brief ec20 版本号查询
 * 
 * @return uint8_t 
 */
uint8_t  ec20_at_version_query(void)
{
		unsigned char read_buff[RX_BUFF_SIZE]={0};
		
		char *write_buff="ATI\r\n";
		
		ec20_drv_write((uint8_t *)write_buff, strlen(write_buff));
		
		if ( ec20_drv_blockread(read_buff, RX_BUFF_SIZE, 5000))
	 {
		    if (strstr((char *)read_buff, "Quectel"))
        {
						log("4G模组--->EC20\r\n");
            return 1;
        }
	 }
	 return 0;
}

/**
 * @brief  查询SIM卡是否正常
 * 
 * @return uint8_t 
 */
uint8_t ec20_at_is_simready(void)
{
	unsigned char read_buff[RX_BUFF_SIZE]={0};
	
	char *write_buff="AT+CPIN?\r\n";
	
	ec20_drv_write((uint8_t *)write_buff, strlen(write_buff));
	
	 if ( ec20_drv_blockread(read_buff, RX_BUFF_SIZE, 5000))
	 {
		    if (strstr((char *)read_buff, "OK"))
        {
            return 1;
        }
				
	 }
	 return 0;
}


/**
 * @brief 是否驻网
 * 
 * @return uint8_t 
 */
uint8_t ec20_at_is_reg(void)
{
	unsigned char read_buff[RX_BUFF_SIZE]={0};
	
	char *write_buff="AT+CGREG?\r\n";
	
	ec20_drv_write((uint8_t *)write_buff, strlen(write_buff));
	
	 if ( ec20_drv_blockread(read_buff, RX_BUFF_SIZE, 5000))
	 {
		    if (strstr((char *)read_buff, "+CGREG: 0,1"))
        {

            return 1;
        }
				if (strstr((char *)read_buff, "+CGREG: 0,5"))
        {

            return 1;
        }
	 }
	 return 0;
}




/**
 * @brief  激活pdp
 * 
 * @return uint8_t 
 */

uint8_t ec20_at_pdp_enable(void)
{
	unsigned char read_buff[RX_BUFF_SIZE]={0};
	
	char *write_buff="AT+QIACT=1\r\n";
	
	ec20_drv_write((uint8_t *)write_buff, strlen(write_buff));
	
	 if ( ec20_drv_blockread(read_buff, RX_BUFF_SIZE, 5000))
	 {
		    if (strstr((char *)read_buff, "OK"))
        {
            return 1;
        }
	 }
	 return 0;
}

/**
 * @brief 去激活pdp
 * 
 * @return uint8_t 
 */
uint8_t ec20_at_pdp_disable(void)
{
	unsigned char read_buff[RX_BUFF_SIZE]={0};

	char *write_buff="AT+QIDEACT=1\r\n";
	
	ec20_drv_write((uint8_t *)write_buff, strlen(write_buff));
	
	 if ( ec20_drv_blockread(read_buff, RX_BUFF_SIZE, 5000))
	 {
		    if (strstr((char *)read_buff, "OK"))
        {
            return 1;
        }
	 }
	 return 0;
}


/**
 * @brief  查询激活pdp的场景和ip地址，一共十六路
 * 
 * @return uint8_t 
 */

uint8_t ec20_at_check_act(void)
{
	unsigned char read_buff[RX_BUFF_SIZE]={0};
	
	char *write_buff="AT+QIACT?\r\n";
	
	ec20_drv_write((uint8_t *)write_buff, strlen(write_buff));
	
	 if ( ec20_drv_blockread(read_buff, RX_BUFF_SIZE, 5000))
	 {
		    if (strstr((char *)read_buff, "+QIACT: 1"))
        {
            return 1;
        }
	 }
	 return 0;
}

/**
 * @brief Get the device time object  获取网络时间
 * 
 * @param temp 
 * @return int 
 */
int ec20_at_get_devicetime(ec20_time *temp)
{
    uint8_t resp_buf[RX_BUFF_SIZE] = {0};

		char *msg ="AT+CCLK?\r\n";
			
    ec20_drv_write((unsigned char *)msg, strlen(msg));
		
    if (ec20_drv_blockread(resp_buf, RX_BUFF_SIZE, 4000))
    {

			if(strstr((char*)resp_buf,"+CCLK:")){

				ec20_time temp_tt;
				
        if (sscanf((char*)resp_buf, "%*[^\"]\"%d/%d/%d,%d:%d:%d", (int *)&(temp_tt.year), (int *)&temp_tt.month, (int *)&temp_tt.day, (int *)&temp_tt.hour, (int *)&temp_tt.minute, (int *)&temp_tt.second) == 6)
        {

            //转换成北京时间
            utc_to_beijing(&temp_tt);

            memcpy(temp, &temp_tt, sizeof(ec20_time));
            return 1;
        }				
				
			}

    }

    return 0;
}


/**
 * @brief 查询ICCID
 * 
 * @return uint8_t 
 */

uint8_t ec20_at_get_iccid(device_iccid_info *iccid)
{
	if(iccid == 0 ) return 0;
	unsigned char read_buff[RX_BUFF_SIZE]={0};
	
	char *write_buff="AT+CCID\r\n";
	
	ec20_drv_write((uint8_t *)write_buff, strlen(write_buff));
	
	 if ( ec20_drv_blockread(read_buff, RX_BUFF_SIZE, 5000))
	 {
		    if (strstr((char *)read_buff, "+CCID:"))
        {
						memcpy(iccid->iccid,read_buff+9,20);
            return 1;
        }
				
				
				//+CME ERROR: 13
	 }
	 return 0;
}


/**
 * @brief 查询IMEI
 * 
 * @return uint8_t 
 */

uint8_t ec20_at_get_imei(device_imei_info *imei)
{
	if(imei == 0 ) return 0;
	unsigned char read_buff[RX_BUFF_SIZE]={0};
	
	char *write_buff="AT+GSN\r\n";
	
	ec20_drv_write((uint8_t *)write_buff, strlen(write_buff));
	
	 if ( ec20_drv_blockread(read_buff, RX_BUFF_SIZE, 5000))
	 {
		    if (strstr((char *)read_buff, "OK"))
        {
						memcpy(imei->imei,read_buff+2,15);
						
            return 1;
        }
	 }
	 return 0;
}




/**
 * @brief onenet先发送下版本号，可能连不上
 * 
 * @param client_idx 
 * @param vsn 
 * @return uint8_t 
 */
uint8_t ec20_at_connect_onenet_version_config(uint8_t client_idx,uint8_t vsn)
{

	unsigned char read_buff[RX_BUFF_SIZE]={0};
	
	//char *write_buff="AT+QMTCFG=\"version\",0,4\r\n";
	
	unsigned char write_buff[256]={0};
	
	sprintf((char *)write_buff,"AT+QMTCFG=\"version\",%d,%d\r\n",client_idx,vsn);
	
	ec20_drv_writestr((char *)write_buff);
	
	 if ( ec20_drv_blockread(read_buff, RX_BUFF_SIZE, 5000))
	 {
		    if (strstr((char *)read_buff, "OK"))
        {	
           return 1;
        }
						
				//+CME ERROR: 13
	 }
	 return 0;
}


/*+QMTCFG: "aliauth",(支持的<client_idx>范围),"productkey","devicename","devicesecret"*/
uint8_t ec20_at_connect_aliauth_config(uint8_t channel,uint8_t *clientid,char *username,char *password)
{

	unsigned char read_buff[RX_BUFF_SIZE]={0};
	
	//char *write_buff="AT+QMTCFG=\"version\",0,4\r\n";
	
	unsigned char write_buff[256]={0};
	
	sprintf((char *)write_buff,"AT+QMTCFG=\"aliauth\",%d,\"%s\",\"%s\",\"%s\"\r\n",channel,clientid,username,password);
	
	ec20_drv_writestr((char *)write_buff);
	
	 if ( ec20_drv_blockread(read_buff, RX_BUFF_SIZE, 5000))
	 {
		    if (strstr((char *)read_buff, "OK"))
        {	
           return 1;
        }
						
				//+CME ERROR: 13
	 }
	 return 0;
}


/**
 * @brief 接收数据包含数据长度
 * 		  state==1 enable state==0 disable
 * @param channel 
 * @param state 
 * @return uint8_t 
 */
uint8_t ec20_at_recv_len_enable(uint8_t channel,uint8_t state)
{

	unsigned char read_buff[RX_BUFF_SIZE]={0};
	
	unsigned char write_buff[256]={0};
	
	sprintf((char *)write_buff,"AT+QMTCFG=\"recv/mode\",%d,0,%d\r\n",channel,state);
	
	ec20_drv_writestr((char *)write_buff);
	
	 if ( ec20_drv_blockread(read_buff, RX_BUFF_SIZE, 5000))
	 {
		    if (strstr((char *)read_buff, "OK"))
        {	
           return 1;
        }
						
				//+CME ERROR: 13
	 }
	 return 0;
}

 
/**
 * @brief 建立tcp连接
 * 
 * @param id  通道号 0-5
 * @param ip  ip号
 * @param port 端口号
 * @return uint8_t 
 */
uint8_t ec20_at_tcp_open(uint8_t  id,uint8_t *ip,uint16_t  port)
{
	
	  unsigned char write_buff[RX_BUFF_SIZE] = {0};
    unsigned char read_buff[RX_BUFF_SIZE] = {0};
		/*onenet*/
		//int write_len = sprintf((char *)write_buff, "AT+QMTOPEN=%d,\"%s\",%d\r\n", id, ip, port);
		
		int write_len = sprintf((char *)write_buff, "AT+QMTOPEN=%d,\"%s\",%d\r\n", id, ip, port);

    ec20_drv_write(write_buff, write_len);
//		+QMTOPEN: 0,0


		if (ec20_drv_blockread(read_buff, RX_BUFF_SIZE, 5000))
		{
			 if (strstr((char *)read_buff, "OK")){
			 
					if(ec20_drv_blockread(read_buff, RX_BUFF_SIZE, 5000)){
					
							int ret=sprintf((char *)write_buff, "+QMTOPEN: %d,0", id);
//							log("ret=%d,write_buff:%s",ret,write_buff);
							if(ret){
							
									if(strstr((char *)read_buff, (char *)write_buff))
									{
											return 1;
											
									}else{
									
										log("read_buff:%s",read_buff);
									}				
									
							}					
						
					}
					
			 }
			
		}	

    return 0;
}


/**
 * @brief 建立tcp连接
 * 
 * @param id  通道号 0-5
 * @param ip  ip号
 * @param port 端口号
 * @return uint8_t 
 */
uint8_t ec20_at_tcp_open_aliauth(uint8_t  id,uint8_t *ip,uint16_t  port)
{
	
	  unsigned char write_buff[RX_BUFF_SIZE] = {0};
    unsigned char read_buff[RX_BUFF_SIZE] = {0};
		/*onenet*/
		//int write_len = sprintf((char *)write_buff, "AT+QMTOPEN=%d,\"%s\",%d\r\n", id, ip, port);
		
		int write_len = sprintf((char *)write_buff, "AT+QMTOPEN=%d,\"%s\",%d\r\n", id, ip, port);

    ec20_drv_write(write_buff, write_len);
//		+QMTOPEN: 0,0


		if (ec20_drv_blockread(read_buff, RX_BUFF_SIZE, 5000))
		{
			 if (strstr((char *)read_buff, "OK")){
			 
					return 1;
					if(ec20_drv_blockread(read_buff, RX_BUFF_SIZE, 5000)){
					
							int ret=sprintf((char *)write_buff, "+QMTOPEN: %d,0", id);
							
//							log("ret=%d,write_buff:%s",ret,write_buff);
							if(ret){
							
									if(strstr((char *)read_buff, (char *)write_buff))
									{
											return 1;
											
									}else {
									
										log("read_buff:%s",read_buff);
									}				
									
							}					
						
					}
					
			 }
			
		}	

    return 0;
}


/****************************onenet**********************************/

/**
 * @brief 关闭tcp连接
 * 
 * @param id  通道号 0-5
 * @param ip  ip号
 * @param port 端口号
 * @return uint8_t 
 *recv:
 *+QMTSTAT: 0,1     只有在连接成功的时候close才会成功
 *
 */
uint8_t ec20_at_tcp_close(char id)
{
	  unsigned char write_buff[RX_BUFF_SIZE] = {0};
    unsigned char read_buff[RX_BUFF_SIZE] = {0};
		
		int write_len = sprintf((char *)write_buff, "AT+QMTCLOSE=%d\r\n",id);

    ec20_drv_write(write_buff, write_len);

    if (ec20_drv_blockread(read_buff, RX_BUFF_SIZE, 5000))
    {

			
			sprintf((char *)write_buff, "+QMTCLOSE: %d,0", id);

			if (strstr((char *)read_buff, (char *)write_buff))
			{
				return 1;
			}


    }
    return 0;
}




/**
 * @brief 建立mqtt连接
 * 
 * @param channel 
 * @param clientid 
 * @param username 
 * @param password 
 * @return uint8_t     
 */
uint8_t ec20_at_mqtt_con(uint8_t channel,uint8_t *clientid,char *username,char *password){
	
    unsigned char write_buff[RX_BUFF_SIZE] = {0};
    unsigned char read_buff[RX_BUFF_SIZE] = {0};
		
		int write_len = sprintf((char *)write_buff, "AT+QMTCONN=%d,\"%s\",\"%s\",\"%s\"\r\n",channel,clientid,username,password);

    ec20_drv_write(write_buff, write_len);

		// 成功会回复 +QMTCONN: channel(通道号),0(0成功1失败),1(ret_code)  
    if (ec20_drv_blockread(read_buff, RX_BUFF_SIZE, 5000))
    {
				if (strstr((char *)read_buff, "OK")){
				
					if(ec20_drv_blockread(read_buff, RX_BUFF_SIZE, 5000)){
						
						sprintf((char *)write_buff, "+QMTCONN: %d,0,0", channel);
						if (strstr((char *)read_buff, (char *)write_buff)){
							
								return 1;
							
						}
					}
				}

    }
    return 1;		
	
}



/**
 * @brief 建立mqtt连接
 * 
 * @param channel 
 * @param clientid 
 * @param username 
 * @param password 
 * @return uint8_t     
 */
uint8_t ec20_at_mqtt_con_aliauth(uint8_t channel,uint8_t *clientid){
	
    unsigned char write_buff[RX_BUFF_SIZE] = {0};
    unsigned char read_buff[RX_BUFF_SIZE] = {0};
		
		int write_len = sprintf((char *)write_buff, "AT+QMTCONN=%d,\"%s\"\r\n",channel,clientid);

    ec20_drv_write(write_buff, write_len);

		// 成功会回复 +QMTCONN: channel(通道号),0(0成功1失败),1(ret_code)  
    if (ec20_drv_blockread(read_buff, RX_BUFF_SIZE, 5000))
    {
				if (strstr((char *)read_buff, "OK")){
				
					if(ec20_drv_blockread(read_buff, RX_BUFF_SIZE, 5000)){
						
						sprintf((char *)write_buff, "+QMTCONN: %d,0,0", channel);
						if (strstr((char *)read_buff, (char *)write_buff)){
							
								return 1;
							
						}
					}
				}

    }
    return 1;		
	
}


/**
 * @brief 断开mqtt连接
 * 
 * @param channel 
 * @param clientid 
 * @param username 
 * @param password 
 * @return uint8_t     
 */
uint8_t ec20_at_mqtt_close(uint8_t channel)
{
	
	
	  unsigned char write_buff[RX_BUFF_SIZE] = {0};
    unsigned char read_buff[RX_BUFF_SIZE] = {0};
		
		int write_len = sprintf((char *)write_buff, "AT+QMTCLOSE=%d\r\n",channel);

    ec20_drv_write(write_buff, write_len);

		// 成功会回复 +QMTCONN: channel(通道号),0(0成功1失败),1(ret_code)  
    if (ec20_drv_blockread(read_buff, RX_BUFF_SIZE, 5000))
    {
				sprintf((char *)write_buff, "+QMTCLOSE: %d,0", channel);
        if (strstr((char *)read_buff, (char *)write_buff))
        {
            return 1;
        }
    }
    return 0;	

}



/**
 * @brief 查询MQTT连接状态
 * 
 * @param channel     
 */
uint8_t ec20_at_mqtt_get_state(uint8_t channel)
{

	    unsigned char read_buff[RX_BUFF_SIZE] = {0};
			unsigned char write_buff[RX_BUFF_SIZE] = {0};
    
		 int write_len = sprintf((char *)write_buff, "AT+QMTCONN?\r\n");

    ec20_drv_write((uint8_t *)write_buff, write_len);

		//+QMTCONN: 0,3
		// 成功会回复 +QMTCONN: channel(通道号),1 mqtt is initial 2,mqtt is connetcting 3 mqttis connected 4mqtt is disconnecting  
    if (ec20_drv_blockread(read_buff, RX_BUFF_SIZE, 5000))
    {		
				
				sprintf((char *)write_buff, "+QMTCONN: %d,3", channel);
				//log("aaaaaaaawrite_buff:%s",write_buff); //千万不要写成  char* write_buff="xxxxxxxx"  sprintf的时候会不成功
        if (strstr((char *)read_buff, (char *)write_buff))
        {
            return 1;
        }
    }
    return 0;	
}


//AT+QMTSUB=<client_idx>,<msgID/65535>,“<topic1>”,<qos1>[,“<topic2>”,<qos2>…]
/*可以同时订阅多个topic，这里只做一个*/
uint8_t ec20_at_mqtt_sub(uint8_t channel,uint8_t * topic,uint8_t qos){

		
		unsigned char read_buff[RX_BUFF_SIZE] = {0};
		unsigned char write_buff[RX_BUFF_SIZE] = {0};
		volatile uint16_t msg_id=0;
		
		msg_id++;
		
		ec20_drv_printf("AT+QMTSUB=%d,%d,\"%s\",%d\r\n",channel,msg_id,topic,qos);
		
		if (ec20_drv_blockread(read_buff, RX_BUFF_SIZE, 5000))
    {
        if (strstr((char *)read_buff, "OK"))
        {
						if (ec20_drv_blockread(read_buff, RX_BUFF_SIZE, 5000)){
						
								sprintf((char *)write_buff, "+QMTSUB: %d,%d,0,%d", channel,msg_id,qos);
//								log("write_buff,sub=%s",write_buff);
								if (strstr((char *)read_buff, (char *)write_buff)){
									
										return 1;
								
								}

						}
            
        }
    }
    return 0;	
		
}




/**
 * @brief mqtt发布
 * 
 * @param channel 
 * @param topic 
 * @param msg 
 * @param length 
 * @return uint8_t       
 */
uint8_t ec20_at_mqtt_pub(uint8_t channel,uint8_t * topic,uint8_t *msg,int length){
	
		if(length > EC20_PUB_MAX){
			
			return 0;
		}
		
			unsigned char write_buff[512]={0};
		unsigned char readbuff[512]={0};


		/*AT+QMTPUBEX=client_idx,msgid,qos,retain,topic,msglen */
		sprintf((char*)write_buff,"AT+QMTPUBEX=%d,0,0,0,\"%s\",%d\r\n",channel,topic,length);
		
		ec20_drv_writestr((char*)write_buff);
		
		char *p = 0;
		
		int ret =ec20_drv_blockread(readbuff,256,3500);
		
		if (ret){
		
				if ((p = strstr((char *)readbuff, ">"))){
			
				
							log("get >>>");
							
							ec20_drv_write(msg,length);
					
							ret = ec20_drv_blockread(readbuff, 256, 5000);{
							if (ret){	
						
									return 1;
						
							}	
							
						}
				
				}
						
							
    return 0;		
	
	}
}

uint8_t  ec20_at_get_ntp_server_time(char *server,ec20_time* temp)
{
    uint8_t resp_buf[RX_BUFF_SIZE] = {0};

		char msg[128]={0};
		
		sprintf(msg,"AT+QNTP=1,\"%s\",123,1\r\n",server);

    ec20_drv_write((unsigned char *)msg, strlen(msg));
		
    if (ec20_drv_blockread(resp_buf, RX_BUFF_SIZE, 4000))
    {
			if(strstr((char*)resp_buf,"OK")){
			
				if (ec20_drv_blockread(resp_buf, RX_BUFF_SIZE, 4000)){
				
					
						if(strstr((char*)resp_buf,"+QNTP: 0,")){
						
								ec20_time temp_tt;
								
								if (sscanf((char*)resp_buf, "%*[^\"]\"%d/%d/%d,%d:%d:%d", (int *)&(temp_tt.year), (int *)&temp_tt.month, (int *)&temp_tt.day, (int *)&temp_tt.hour, (int *)&temp_tt.minute, (int *)&temp_tt.second) == 6){
								
										utc_to_beijing(&temp_tt);
										
										memcpy(temp, &temp_tt, sizeof(ec20_time));
										
										return 1;
								}
								
						}
				
				}
				
				
			}

    }

    return 0;
}



void ec20_lib_network_init(ec20_network* _network){


		_network->tick=1000;
		_network->check_state.at_state=0;
		_network->check_state.call_state=0;
		_network->check_state.reg_state=0;
		_network->check_state.sim_state=0;
}



void ec20_lib_network_tick(ec20_network* _network,uint8_t tick)
{
		_network->tick= _network->tick-tick;
		
		if(_network->tick <= tick){
		
				_network->task_call=1;
		}
}


void ec20_lib_network_tick_update(ec20_network* _network)
{
	_network->tick=6000;
}



void ec20_lib_network_clear(ec20_network* _network)
{
	_network->check_state.reg_state=0;
	_network->check_state.at_state=0;
	_network->check_state.call_state=0;
	_network->check_state.sim_state=0;
	_network->tick=1000;
	
}


void ec20_lib_network_event_add(ec20_network *_network,void(*at_event_cb)(uint8_t state),void(*sim_event_cb)(uint8_t state),void(*reg_event_cb)(uint8_t state),void(*call_event_cb)(ec20_time* time)){

		if(_network){
					
				_network->event.at_ready_callback=at_event_cb;						

				_network->event.sim_ready_callback=sim_event_cb;					

				_network->event.reg_ready_callback=reg_event_cb;						

				_network->event.call_ready_callback=call_event_cb;						

		}

}


uint8_t  ec20_lib_network_get_at_state(ec20_network* _network){

	return _network->check_state.at_state;
}

uint8_t  ec20_lib_network_get_sim_state(ec20_network* _network){

	return _network->check_state.sim_state;
	
}

uint8_t  ec20_lib_network_get_reg_state(ec20_network* _network){

	return _network->check_state.reg_state;
	
}

uint8_t  ec20_lib_network_get_call_state(ec20_network* _network){

	return _network->check_state.call_state;
	
}


uint8_t ec20_lib_network_get_imei(ec20_network* _network,device_imei_info* imei)
{
		if(_network->check_state.reg_state){
		
				memcpy(imei,_network->imei.imei,15);
				return 1;
		}
		
		return 0;
}







void ec20_lib_network_process(ec20_network* _network){

			
			if(_network->task_call){
			
					_network->task_call=0;
							
					/*先检查有没有模组*/
					if(_network->check_state.at_state==0){
					
							if(ec20_at_test()){
							
									_network->check_state.at_state=1;
									
									/*如果当前的状态不等于回调的状态*/
									if(_network->check_state.at_state!=_network->event.at_record_state){
											
											
											if(_network->event.at_ready_callback){
//													log("当前状态跟记录状态不一致，进入回调");
													/*更新回调状态*/
													_network->event.at_record_state=_network->check_state.at_state;
													/*触发回调*/
													_network->event.at_ready_callback(1);
											}										
									}


							}else{
							
									_network->check_state.at_state=0;
									
									_network->tick=2000;
									
									/*如果当前的状态不等于回调的状态*/
									if(_network->check_state.at_state!=_network->event.at_record_state){
									
											if(_network->event.at_ready_callback){
													/*更新回调状态*/
													_network->event.at_record_state=_network->check_state.at_state;
													/*触发回调*/
													_network->event.at_ready_callback(0);
											}										
									}
									
									return ;	

							}
					}
					
		      /*如果检测到模组，再检测有没有卡*/			
					if(_network->check_state.at_state){
					
							if(_network->check_state.sim_state==0){
					
							int ret = ec20_at_is_simready();

							if(ec20_at_is_simready()){
							
									_network->check_state.sim_state=1;
									
									/*如果当前的状态不等于回调的状态*/
									if(_network->check_state.sim_state!=_network->event.sim_record_state){
									
											if(_network->event.sim_ready_callback){
													/*更新回调状态*/
													_network->event.sim_record_state=_network->check_state.sim_state;
													/*触发回调*/
													_network->event.sim_ready_callback(1);
											}										
									}


							}else{
							
									_network->check_state.sim_state=0;
									
									_network->tick=2000;
									
									/*如果当前的状态不等于回调的状态*/
									/*如果初始化的时候把SIM卡的状态初始化为0，这条是触发不了的*/
									if(_network->check_state.sim_state!=_network->event.sim_record_state){
									
											if(_network->event.sim_ready_callback){
													/*更新回调状态*/
													_network->event.sim_record_state=_network->check_state.sim_state;
													/*触发回调*/
													_network->event.sim_ready_callback(0);
											}										
									}
									
									return ;	

							}
					}				
						
			 }	

			/*如果检测到sim，再检测有没有驻网*/			
			if(_network->check_state.sim_state){
			
					if(_network->check_state.reg_state==0){
			
							if(ec20_at_is_reg()){
							
									uint8_t iccid_ret= ec20_at_get_iccid(&_network->iccid);
									uint8_t imei_ret = ec20_at_get_imei(&_network->imei);
									//确保imei和iccid捕获成功，才进入下一阶段
									if(iccid_ret && imei_ret){
										
//											log("imei=%s,iccid=%s",_network->imei.imei,_network->iccid.iccid);
											_network->check_state.reg_state=1;
									}
												
										/*如果当前的状态不等于回调的状态*/
									if(_network->check_state.reg_state!=_network->event.reg_record_state){
									
											if(_network->event.reg_ready_callback){
													/*更新回调状态*/
													_network->event.reg_record_state=_network->check_state.reg_state;
													/*触发回调*/
													_network->event.reg_ready_callback(1);
											}										
									}					
											
							
									

							}else{
							
									_network->check_state.reg_state=0;
									
									_network->tick=2000;
									
									if(_network->check_state.reg_state!=_network->event.reg_record_state){
									
											if(_network->event.reg_ready_callback){
													/*更新回调状态*/
													_network->event.reg_record_state=_network->check_state.reg_state;
													/*触发回调*/
													_network->event.reg_ready_callback(0);
											}										
									}
									
									return ;	

							}
			}				
				
		}	
		
		/*如果驻网了，再检测有没有打CALL*/			
		if(_network->check_state.reg_state){
								 
					if(_network->check_state.call_state==0){
			
							if(ec20_at_pdp_enable()){
							
									_network->check_state.call_state=1;
												
									/*如果当前的状态不等于回调的状态*/
									if(_network->check_state.call_state!=_network->event.call_record_state){
									
											if(_network->event.call_ready_callback){
													/*更新回调状态*/
													_network->event.call_record_state=_network->check_state.call_state;
													
													ec20_time time={0};
													if(!ec20_at_get_ntp_server_time("ntp.aliyun.com",&time)){
													
															//ntp获取失败时，用cclk替代
															ec20_at_get_devicetime(&time);	
															
													}
													/*触发回调*/
													_network->event.call_ready_callback(&time);
											}										
									}


							}else{
							
							
									_network->check_state.call_state=0;
												
									_network->tick=2000;
									
									/*去激活一下，成功率更高*/
									ec20_at_pdp_disable();

									if(_network->check_state.call_state!=_network->event.call_record_state){
									
											if(_network->event.call_ready_callback){
													/*更新回调状态*/
													_network->event.call_record_state=_network->check_state.call_state;
													
													ec20_time time={0};
													if(!ec20_at_get_ntp_server_time("ntp.aliyun.com",&time)){
														
															//ntp获取失败时，用cclk替代
															ec20_at_get_devicetime(&time);	
													
													}
													/*触发回调*/
													_network->event.call_ready_callback(0);
											}										
									}
									
									return ;	

							}
			}	

      _network->check_state.call_state=1;	
			ec20_at_check_act();			
			_network->tick=10000;	
		}	
	}		
}







void ec20_lib_mqtt_tick(ec20_mqtt_handle* mqtt_handle,uint32_t tick){

		if(mqtt_handle){
		
			if(mqtt_handle->task_tick){
			
					mqtt_handle->task_tick-=tick;
					
					if(mqtt_handle->task_tick<tick){
							/*如果连接上了，tick=10s*/
							if(mqtt_handle->con_state){
							
									mqtt_handle->task_call=1;									
									mqtt_handle->task_tick=20000;
									
							}else{
									/*如果没连上，tick=2s*/
									mqtt_handle->task_call=1;
									mqtt_handle->task_tick=2000;
							
							}
					}
			
			}
		}

}



static void ec20_lib_mqtt_freash_tick(ec20_mqtt_handle * mqhandle,int tick){
	
		mqhandle->task_tick = tick;
}



uint8_t ec20_lib_mqtt_init(ec20_mqtt_handle* mqtt_handle,uint8_t channel,uint8_t *ip,uint16_t port,uint8_t*clientid,uint8_t*username,uint8_t*password){
		
		if(mqtt_handle){
		
			if(strlen((char*)ip)<MQTT_MON_IP_MAX){
			
					strcpy((char*)mqtt_handle->param.ip,(char*)ip);
					
					
			}else{
			
					return 0;	
			}
			
			if(strlen((char*)clientid)<MQTT_MON_CID_MAX){
			
					strcpy((char*)mqtt_handle->param.clientid,(char*)clientid);
					
			}else{
			
					return 0;
			}	

			if(strlen((char*)clientid)<MQTT_MON_UNAME_MAX){
			
					strcpy((char*)mqtt_handle->param.username,(char*)username);
					
			}else{
			
					return 0;	
			} 
		
			if(strlen((char*)clientid)<MQTT_MON_PSD_MAX){
			
					strcpy((char*)mqtt_handle->param.password,(char*)password);
					
			}else{
			
					return 0;
			}	
			
			mqtt_handle->param.port=port;
			
			mqtt_handle->state.con_state=0;
			mqtt_handle->task_tick=1000;
			mqtt_handle->con_task=1; //默认开机连接
			mqtt_handle->param.channel=channel;
			
//			memset(mqtt_handle->,0,MQTT_MON_TOPIC_MAX);
		
		}
		return 1;
}


void ec20_lib_mqtt_process(ec20_mqtt_handle * mqtt_handle){

		if(mqtt_handle==0){
		
				return ;
		}
		
		if(mqtt_handle->task_call){
		
				
				mqtt_handle->task_call=0;
				
				if(mqtt_handle->con_task){
				
						
						if(mqtt_handle->new_state==0){
						
						
								int ret =ec20_at_tcp_open(mqtt_handle->param.channel,mqtt_handle->param.ip,mqtt_handle->param.port);
								
								if(ret){
								
										mqtt_handle->new_state=1;
										
								}else{
								
										ec20_at_tcp_close(mqtt_handle->param.channel);
								
								}
							

						}
						
						if(mqtt_handle->new_state &&mqtt_handle->con_state==0){
						
								
									int ret = ec20_at_mqtt_con(mqtt_handle->param.channel,mqtt_handle->param.clientid,(char*)mqtt_handle->param.username,(char*)mqtt_handle->param.password);
									

								
								if(ret){
								
										mqtt_handle->new_state=1;
										mqtt_handle->con_state=1;
										
										if(mqtt_handle->callback.concb){
										
												mqtt_handle->callback.concb(1);
												
										}
										
										ec20_lib_mqtt_freash_tick(mqtt_handle,2000);
										
										return ;
										
								}else{
								
										mqtt_handle->new_state=0;
										mqtt_handle->con_state=0;
										log("mqtt close");
										ec20_at_mqtt_close(mqtt_handle->param.channel);
										
										if(mqtt_handle->callback.concb){
										
												mqtt_handle->callback.concb(0);
										}
								}
							
						}
						
						
						if(mqtt_handle->new_state && mqtt_handle->con_state){
						
								int ret = ec20_at_mqtt_get_state(mqtt_handle->param.channel);
								
								if(ret==0){
								
										mqtt_handle->new_state=0;
										mqtt_handle->con_state=0;
										/*如果想要立马重连，可以加上这一句，如果想要轮询到下一个周期再连接，屏蔽掉*/
										mqtt_handle->task_call=1;
										log("querry offline");
										
										if(mqtt_handle->callback.concb){
											
												mqtt_handle->callback.concb(0);
												
										}
										
								}
															
						}
				}else{//如果当前是记录的在线，就进行下线
				
				
							if(mqtt_handle->con_state){
							
									int ret = ec20_at_mqtt_get_state(mqtt_handle->param.channel);
									
									if(ret==0){
										
											mqtt_handle->new_state=0;
											mqtt_handle->con_state=0;
											log("close mqtt, offline");
											if(mqtt_handle->callback.concb){
											
													mqtt_handle->callback.concb(0);
											}
											
											
									}else{
									
											ec20_at_mqtt_close(mqtt_handle->param.channel);
											if(ec20_at_mqtt_get_state(mqtt_handle->param.channel)==0){
											
													mqtt_handle->new_state=0;
													mqtt_handle->con_state=0;
													log("close mqtt, offline");
													if(mqtt_handle->callback.concb){
													
															mqtt_handle->callback.concb(0);
													}
											}
									}
							}
					
				}
				
		}
			
}

void ec20_lib_mqtt_process_aliauth(ec20_mqtt_handle * mqtt_handle){

		if(mqtt_handle==0){
		
				return ;
		}
		
		if(mqtt_handle->task_call){
		
				
				mqtt_handle->task_call=0;
				
				if(mqtt_handle->con_task){
				
						
						if(mqtt_handle->new_state==0){
							
							
										/*配置aliauth参数*/
							if(ec20_at_connect_aliauth_config(mqtt_handle->param.channel,mqtt_handle->param.clientid,(char*)mqtt_handle->param.username,(char*)mqtt_handle->param.password)){
									
									log("阿里云参数配置成功");
									
									int ret = ec20_at_tcp_open(mqtt_handle->param.channel,mqtt_handle->param.ip,mqtt_handle->param.port);
									
									log("tcp open ret =%d",ret);
									
									if(ret){
									
											log("阿里云连接成功");
							
											mqtt_handle->new_state=1;
									
									}else{
									
											if(ec20_at_mqtt_close(mqtt_handle->param.channel)){
											
												
													log("阿里云关闭成功");
											}
							
									}
									
							}
						}
						
						if(mqtt_handle->new_state &&mqtt_handle->con_state==0){
						
							
									
									int ret = ec20_at_mqtt_con_aliauth(mqtt_handle->param.channel,mqtt_handle->param.clientid);
								
				
								
								if(ret){
								
										mqtt_handle->new_state=1;
										mqtt_handle->con_state=1;
										
										if(mqtt_handle->callback.concb){
										
												mqtt_handle->callback.concb(1);
												
										}
										
										ec20_lib_mqtt_freash_tick(mqtt_handle,2000);
										
										return ;
										
								}else{
								
										mqtt_handle->new_state=0;
										mqtt_handle->con_state=0;
										log("mqtt close");
										ec20_at_mqtt_close(mqtt_handle->param.channel);
										
										if(mqtt_handle->callback.concb){
										
												mqtt_handle->callback.concb(0);
										}
								}
							
						}
						
						
						if(mqtt_handle->new_state && mqtt_handle->con_state){
						
								int ret = ec20_at_mqtt_get_state(mqtt_handle->param.channel);
								
								if(ret==0){
								
										mqtt_handle->new_state=0;
										mqtt_handle->con_state=0;
										/*如果想要立马重连，可以加上这一句，如果想要轮询到下一个周期再连接，屏蔽掉*/
										mqtt_handle->task_call=1;
										log("querry offline");
										
										if(mqtt_handle->callback.concb){
											
												mqtt_handle->callback.concb(0);
												
										}
										
								}
															
						}
				}else{//如果当前是记录的在线，就进行下线
				
				
							if(mqtt_handle->con_state){
							
									int ret = ec20_at_mqtt_get_state(mqtt_handle->param.channel);
									
									if(ret==0){
										
											mqtt_handle->new_state=0;
											mqtt_handle->con_state=0;
											log("close mqtt, offline");
											if(mqtt_handle->callback.concb){
											
													mqtt_handle->callback.concb(0);
											}
											
											
									}else{
									
											ec20_at_mqtt_close(mqtt_handle->param.channel);
											if(ec20_at_mqtt_get_state(mqtt_handle->param.channel)==0){
											
													mqtt_handle->new_state=0;
													mqtt_handle->con_state=0;
													log("close mqtt, offline");
													if(mqtt_handle->callback.concb){
													
															mqtt_handle->callback.concb(0);
													}
											}
									}
							}
					
				}
				
		}
			
}

uint8_t ec20_lib_mqtt_regcb(ec20_mqtt_handle * mqtt_handle,void (*msgcb)(uint8_t* topic,uint8_t *data,int len),void (*concb)(uint8_t state)){
	
	if(mqtt_handle){
		
		mqtt_handle->callback.msgcb=msgcb;
		mqtt_handle->callback.concb=concb;
		
	}
	return 0;
}


uint8_t ec20_lib_mqtt_close_wait(ec20_mqtt_handle * mqtt_handle,uint8_t channel){
	
//	if(mqtt_handle){
//		
//		mqtt_handle->con_task = 1;
//		
////		gm190_mqtt_close(channel);
//		
//		mqtt_handle->con_state = 0;
//	}	
//			
}

uint8_t ec20_lib_mqtt_control(ec20_mqtt_handle * mqhandle,uint8_t state){
	
	if(mqhandle){
		
		mqhandle->con_task = state;
		log("set mqhandle->con_task :%d",mqhandle->con_task);
		mqhandle->task_tick=1000;
		mqhandle->task_call=1;
	}
	return 0;
}





/*这是一个处理GM190模块接收到的MQTT消息的函数。
*它接受三个参数：一个指向gm190_mqtt_handle结构体的指针
*一个指向包含消息的char数组的指针，和一个表示消息长度的int值。
*函数返回一个uint8_t值，表示消息是否被处理。
*/
uint8_t ec20_lib_mqtt_yeild(ec20_mqtt_handle * mqhandle,char *msg, int length){
	
		//声明了两个char数组，用来存储接收和断开连接消息的钩子字符串。
		char hook_buff[15]={0};
		char hook_buff2[15]={0};
		
		char *p1 = NULL;
		char *p2 = NULL;
		
		//sprintf来格式化钩子字符串，用gm190_mqtt_handle结构体中的通道号填充。
		sprintf(hook_buff,"+QMTRECV: %d",mqhandle->param.channel);
		sprintf(hook_buff2,"+QMTSTAT: %d,1",mqhandle->param.channel);
		
		//声明了一个int变量，用来存储消息长度，和一个uint8_t数组，用来存储主题名称。
		int msg_len=0;	
		uint8_t topic[EC20_TOPIC_MAX]={0};
		
		//使用strstr来在消息中查找接收钩子字符串的第一次出现。
		char *p = strstr(msg, hook_buff);
		
		//找到了，它使用sscanf来解析消息，提取主题名称和消息长度。
		if(p){
		
			//使用sscanf函数来扫描p指向的消息，并提取主题名称和消息长度。
			//格式字符串"+ZMQRCV:%*d,%[^,],%*d,%*d,%*d,%d"告诉函数忽略"+ZMQRCV:"后面的第一个整数，
			//然后复制直到下一个逗号之前的所有内容到主题数组，然后忽略接下来的三个整数，
			//然后把最后一个整数存储到msg_len变量。函数返回成功扫描的项目数，这里应该是2。
			//GM331/190  ---->/+ZMQRCV: 1,mytopic,1,0,0,4,1234
			//EC20 ------>		+QMTRECV: 1,0,"hello",4,"haha" 
			int ret = sscanf(p,"+QMTRECV:%*d,%*d,\"%[^\"]\",%d",topic,&msg_len);
			
			//使用一个自定义函数find_pos来定位消息中第3个逗号的位置，这标志着有效载荷的开始。
			if(ret == 2){
				
				
				
				int offset = find_pos(p,',',4);
				
	
				// 如果找到了，表示有效载荷的开始
				 if(offset) {
	
					*(p+offset+msg_len+2)=0;
					
		
					//检查ec20_mqtt_handle结构体中是否注册了一个用于处理消息的回调函数。如果有，它用主题名称和有效载荷作为参数调用它。
					if(mqhandle->callback.msgcb)
							mqhandle->callback.msgcb(topic,(uint8_t*)(p+offset+2),msg_len);
					
				}
			}
			return 1;//返回1，表示消息被处理。
			
		}else {//没有找到接收钩子字符串，它使用strstr再次在消息中查找断开连接钩子字符串的第一次出现
			
				p = strstr(msg, hook_buff2);//找到了，它将gm190_mqtt_handle结构体中的连接状态、新状态和任务调用标志设置为零。
				
				if(p){ //然后记录一条消息，说“捕获到离线钩子”。
					
					mqhandle->con_state = 0;
					mqhandle->new_state = 0;
					mqhandle->task_call=1;
					
					log("catch offline hook");
					log("hook_buff2=%s",hook_buff2);
					if(mqhandle->callback.concb)mqhandle->callback.concb(0);//检查gm190_mqtt_handle结构体中是否注册了一个用于处理断开连接的回调函数
					
				}			
			
		}
    return 0;
		
}


uint8_t ec20_lib_mqtt_topic_set(ec20_mqtt_handle * mqtt_handle,uint8_t id,char *msg,uint8_t len){

	if(mqtt_handle){
		
		if(len<TOPIC_MAX){
			
			if(id<TOPIC_NUM){
			
					memcpy(mqtt_handle->topic.topics[id].topic,msg,len);			
					return 1;
			}else{
			
					log("topic id 超限制");
					return 0;
			}

		}else{
		
			log("topic长度超限制");
			return 0;
		}
	
	}
	
	return 0;
		
}


char* ec20_lib_mqtt_topic_get(ec20_mqtt_handle * mqtt_handle,uint8_t id){
		
		return mqtt_handle->topic.topics[id].topic;	
			
}


