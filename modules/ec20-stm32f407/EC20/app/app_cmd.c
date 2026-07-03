#include "app_cmd.h"
#include "app_at_frame.h"
#include "bsp_usart.h"

#include "app_gather.h"
#include "app_flashid.h"
#include "app_flashlite.h"
#include "app_info.h"
#include "ec20_network.h"
#include "ec20_lib.h"
#include "ec20_mqtt.h"

#include "log.h"

#include <stdarg.h>				
#include <string.h>				
#include <stdio.h>				
#include <stdlib.h>

#define READ_PARAM(ID, BUFF) 					app_flash_read(ID, BUFF, get_flashid_size(ID))
#define WRITE_PARAM(ID, BUFF, SIZE) 	app_flash_write(ID, BUFF, SIZE)
#define WRITE_STRPARAM(ID, BUFF) 			app_flash_write(ID, BUFF, strlen(BUFF))
#define DELETE(ID) 										app_flash_write(ID, 0, 0)

int app_task_ati(uint8_t *message, uint16_t size, ATInfo *info);
int app_task_at_config(uint8_t *message, uint16_t size, ATInfo *info);
int app_task_at_rest(uint8_t *message, uint16_t size, ATInfo *info);
int app_task_at_rs485(uint8_t *message, uint16_t size, ATInfo *info);
int app_task_at_onenet(uint8_t *message, uint16_t size, ATInfo *info);
int app_task_at_aliauth(uint8_t *message, uint16_t size, ATInfo *info);
int app_task_at_seting(uint8_t *message, uint16_t size, ATInfo *info);
int app_task_at_test(uint8_t *message, uint16_t size, ATInfo *info);
int app_task_at_relay(uint8_t *message, uint16_t size, ATInfo *info);
int app_task_at_input(uint8_t *message, uint16_t size, ATInfo *info);
int app_task_at_imei(uint8_t *message, uint16_t size, ATInfo *info);
int app_task_at_clear(uint8_t *message, uint16_t size, ATInfo *info);
int app_task_at_log(uint8_t *message, uint16_t size, ATInfo *info);
int app_task_at_relay_uploadset(uint8_t *message, uint16_t size, ATInfo *info);
int app_task_at_topic(uint8_t *message, uint16_t size, ATInfo *info);


const CMD_TASK g_app_atcmd_list[] = {

	  {"ATI"						, app_task_ati								, USART_AT_IN | TCP_AT_IN },
		{"AT+CONFIG"			, app_task_at_config					, USART_AT_IN | TCP_AT_IN },
		{"AT+REST"				, app_task_at_rest						, USART_AT_IN | TCP_AT_IN },	
		{"AT+RS485"	   	  , app_task_at_rs485						, USART_AT_IN | TCP_AT_IN },
		{"AT+ONENET"	    , app_task_at_onenet					, USART_AT_IN | TCP_AT_IN },
		{"AT+ALIAUTH"	    , app_task_at_aliauth					, USART_AT_IN | TCP_AT_IN },
		{"AT+SETING"	    , app_task_at_seting					, USART_AT_IN | TCP_AT_IN },
		{"AT+TEST"	    	, app_task_at_test						, USART_AT_IN | TCP_AT_IN },
		{"AT+RELAY"	    	, app_task_at_relay						, USART_AT_IN | TCP_AT_IN },	
		{"AT+INPUT"	      , app_task_at_input						, USART_AT_IN | TCP_AT_IN },		
		{"AT+IMEI"	    	, app_task_at_imei						, USART_AT_IN | TCP_AT_IN },
		{"AT+CLEAR"	    	, app_task_at_clear						, USART_AT_IN | TCP_AT_IN },
		{"AT+LOG"	      	, app_task_at_log			  			, USART_AT_IN | TCP_AT_IN },
		{"AT+RELAYSET"	  , app_task_at_relay_uploadset	, USART_AT_IN | TCP_AT_IN },
		{"AT+TOPIC"	  		, app_task_at_topic						, USART_AT_IN | TCP_AT_IN },
}; 


int app_callback_printf(DATA_IN_MODE mode, char *format, ...)
{
    va_list args;
    unsigned int length;
    unsigned char txbuf[256] = {0};

    va_start(args, format);
    length = vsnprintf((char *)txbuf, sizeof(txbuf), (char *)format, args);
    va_end(args);

		//debug_printf("app_callback_printf:%d",mode);
		
    if (mode == USART_AT_IN)
    {
        debug_write( txbuf, length);
    }

    return 1;
}

/**
 * @brief
 *
 * @param data
 * @param data_size
 * @param at_in
 * @return int
 */
int app_at_process(uint8_t *data, uint16_t data_size, DATA_IN_MODE at_in)
{
    return check_at_process((CMD_TASK *)g_app_atcmd_list, sizeof(g_app_atcmd_list) / sizeof(CMD_TASK), data, data_size, at_in);
}



int app_task_ati(uint8_t *message, uint16_t size, ATInfo *info)
{

	  app_callback_printf(info->at_in,"+ITD=\"2023-5-23-13:56 V1.0\"\r\n");
		
		return 1;

}

int app_task_at_config(uint8_t *message, uint16_t size, ATInfo *info)
{
				int state = 0;

        if (sscanf((char *)message, "AT+CONFIG=\"%d\"\r\n", &state) == 1)
        {
						if(state){
							
										
							
						}else{
							


						}
           app_callback_printf(info->at_in, "+CONFIG=\"%d\"\r\nOK\r\n",state);

            return 1;
        }

    

    return 1;
}


int app_task_at_rest(uint8_t *message, uint16_t size, ATInfo *info)
{
				int state = 0;

        if (strstr((char *)message, "AT+REST"))
        {

            app_callback_printf(info->at_in, "+REST=\"%d\"\r\nOK\r\n",state);
						NVIC_SystemReset();
            return 1;
        }
    return 1;
}


int app_task_at_rs485(uint8_t *message, uint16_t size, ATInfo *info)
{
				int baud = 0;
				
				if (sscanf((char *)message, "AT+RS485=\"%d\"\r\n",&baud))
        {
						if((baud!=4800) &&(baud!=9600) &&(baud!=115200)){
						
								app_callback_printf(info->at_in, "请设置波特率为:4800,9600,115200\r\n");
								return 0;		
						}else{
						

								app_flash_write(RS485_BUAD,(uint8_t*)&baud,4);
								app_callback_printf(info->at_in, "+RS485=\"%d\"\r\nOK\r\n",baud);
								return 1;
						}

        }else if(strstr((char *)message,"AT+RS485?\r\n")){
					
						app_flash_read(RS485_BUAD,(uint8_t*)&baud,4);
					
					  app_callback_printf(info->at_in, "+RS485=\"%d\"\r\nOK\r\n",baud);
				}
    return 1;
}

int app_task_at_onenet(uint8_t *message, uint16_t size, ATInfo *info)
{

				uint8_t add_buff[ONENET_ADDRESS_LEN+1]={0};
				uint8_t clientid_buff[ONENET_CLIENTID_LEN+1]={0};
				uint8_t username_buff[ONENET_USERNAME_LEN+1]={0};
				uint8_t password_buff[ONENET_PASSWORD_LEN+1]={0};
				int port=0;
				
				//ram check
				if (sscanf((char *)message, "AT+ONENET=\"%[^\"]\",\"%d\",\"%[^\"]\",\"%[^\"]\",\"%[^\"]\"",add_buff,&port,clientid_buff,username_buff,password_buff)==5)
        {

						app_flash_write(ONENET_ADDRESS,(uint8_t*)add_buff,ONENET_ADDRESS_LEN);
					  app_flash_write(ONENET_PORT,(uint8_t*)&port,4);
					  app_flash_write(ONENET_CLIENTID,(uint8_t*)clientid_buff,ONENET_CLIENTID_LEN);
					  app_flash_write(ONENET_USERNAME,(uint8_t*)username_buff,ONENET_USERNAME_LEN);
						app_flash_write(ONENET_PASSWORD,(uint8_t*)password_buff,ONENET_PASSWORD_LEN);
					
            app_callback_printf(info->at_in, "+ONENET=\"%s\",\"%d\",\"%s\",\"%s\",\"%s\"\r\nOK\r\n",add_buff,port,clientid_buff,username_buff,password_buff);

            return 1;
					
        }else if(strstr((char *)message,"AT+ONENET?\r\n")){
					
						app_flash_read(ONENET_ADDRESS,(uint8_t*)add_buff,ONENET_ADDRESS_LEN);
					  app_flash_read(ONENET_PORT,(uint8_t*)&port,4);
					  app_flash_read(ONENET_CLIENTID,(uint8_t*)clientid_buff,ONENET_CLIENTID_LEN);
					  app_flash_read(ONENET_USERNAME,(uint8_t*)username_buff,ONENET_USERNAME_LEN);
						app_flash_read(ONENET_PASSWORD,(uint8_t*)password_buff,ONENET_PASSWORD_LEN);
					
            app_callback_printf(info->at_in, "+ONENET=\"%s\",\"%d\",\"%s\",\"%s\",\"%s\"\r\nOK\r\n",add_buff,port,clientid_buff,username_buff,password_buff);
					
					return 1;
				}
    return 1;
}



/*ALIAUTH aliauth*/
int app_task_at_aliauth(uint8_t *message, uint16_t size, ATInfo *info)
{

				uint8_t add_buff[ALIAUTH_ADDRESS_LEN+1]={0};
				uint8_t clientid_buff[ALIAUTH_CLIENTID_LEN+1]={0};
				uint8_t username_buff[ALIAUTH_USERNAME_LEN+1]={0};
				uint8_t password_buff[ALIAUTH_PASSWORD_LEN+1]={0};
				int port=0;
				
				//ram check
				if (sscanf((char *)message, "AT+ALIAUTH=\"%[^\"]\",\"%d\",\"%[^\"]\",\"%[^\"]\",\"%[^\"]\"",add_buff,&port,clientid_buff,username_buff,password_buff)==5) 

        {
				
				
						app_flash_write(ALIAUTH_ADDRESS,(uint8_t*)add_buff,ALIAUTH_ADDRESS_LEN);
					  app_flash_write(ALIAUTH_PORT,(uint8_t*)&port,4);
					  app_flash_write(ALIAUTH_CLIENTID,(uint8_t*)clientid_buff,ALIAUTH_CLIENTID_LEN);
					  app_flash_write(ALIAUTH_USERNAME,(uint8_t*)username_buff,ALIAUTH_USERNAME_LEN);
						app_flash_write(ALIAUTH_PASSWORD,(uint8_t*)password_buff,ALIAUTH_PASSWORD_LEN);
					
            app_callback_printf(info->at_in, "+ALIAUTH=\"%s\",\"%d\",\"%s\",\"%s\",\"%s\"\r\nOK\r\n",add_buff,port,clientid_buff,username_buff,password_buff);

            return 1;
					
        }else if(strstr((char *)message,"AT+ALIAUTH?\r\n")){
					
						app_flash_read(ALIAUTH_ADDRESS,(uint8_t*)add_buff,ALIAUTH_ADDRESS_LEN);
					  app_flash_read(ALIAUTH_PORT,(uint8_t*)&port,4);
					  app_flash_read(ALIAUTH_CLIENTID,(uint8_t*)clientid_buff,ALIAUTH_CLIENTID_LEN);
					  app_flash_read(ALIAUTH_USERNAME,(uint8_t*)username_buff,ALIAUTH_USERNAME_LEN);
						app_flash_read(ALIAUTH_PASSWORD,(uint8_t*)password_buff,ALIAUTH_PASSWORD_LEN);
					
            app_callback_printf(info->at_in, "+ALIAUTH=\"%s\",\"%d\",\"%s\",\"%s\",\"%s\"\r\nOK\r\n",add_buff,port,clientid_buff,username_buff,password_buff);
					
					return 1;
				}
    return 1;
}


int app_task_at_seting(uint8_t *message, uint16_t size, ATInfo *info)
{

//			OPERATION_MODE,//设备工作模式，自动 0 ，手动 1
//		MANUAL_MODE_ACQ_FRE,//手动模式的采集频率
//	  MANUAL_MODE_TRANS_FRE,//手动模式的传输频率
//	  AUTO_MODE_ACQ_FRE,//自动模式的默认采集频率
//		AUTO_MODE_TRANS_FRE,//自动默认传输频率
//		THRESHOLD_LOW,//阈值小值
//		THRESHOLD_HIGH,//阈值大值
//		RADAR_BLIND,//雷达盲区数据
	
				int operation_mode;
				int manual_mode_acq_fre=0;
				int manual_mode_trans_fre=0;
				int auto_mode_acq_fre=0;
				int auto_mode_trans_fre=0;	
				int threshold_low=0;	
				int threshold_high=0;
				int radar_blind=0;
//	
//				//int port=0;
//				
//				//ram check
				if (sscanf((char *)message, "AT+SETING=\"%d\",\"%d\",\"%d\",\"%d\",\"%d\",\"%d\",\"%d\",\"%d\"",&operation_mode,&manual_mode_acq_fre,& manual_mode_trans_fre,&auto_mode_acq_fre,&auto_mode_trans_fre,&threshold_low,&threshold_high,&radar_blind)==8)
        {

						app_flash_write(OPERATION_MODE				,(uint8_t*)&operation_mode					,4);
					  app_flash_write(MANUAL_MODE_ACQ_FRE		,(uint8_t*)&manual_mode_acq_fre			,4);
					  app_flash_write(MANUAL_MODE_TRANS_FRE	,(uint8_t*)&manual_mode_trans_fre		,4);
					  app_flash_write(AUTO_MODE_ACQ_FRE			,(uint8_t*)&auto_mode_acq_fre				,4);
						app_flash_write(AUTO_MODE_TRANS_FRE		,(uint8_t*)&auto_mode_trans_fre			,4);
					  app_flash_write(THRESHOLD_LOW					,(uint8_t*)&threshold_low						,4);
					  app_flash_write(THRESHOLD_HIGH				,(uint8_t*)&threshold_high					,4);
						app_flash_write(RADAR_BLIND						,(uint8_t*)&radar_blind							,4);					
					
					
            app_callback_printf(info->at_in, "+SETING=\"%d\",\"%d\",\"%d\",\"%d\",\"%d\",\"%d\",\"%d\",\"%d\"",operation_mode,manual_mode_acq_fre, manual_mode_trans_fre,auto_mode_acq_fre,auto_mode_trans_fre,threshold_low,threshold_high,radar_blind);

            return 1;
					
        }else if(strstr((char *)message,"AT+SETING?\r\n")){
					
						app_flash_read(OPERATION_MODE				,(uint8_t*)&operation_mode					,4);
					  app_flash_read(MANUAL_MODE_ACQ_FRE		,(uint8_t*)&manual_mode_acq_fre			,4);
					  app_flash_read(MANUAL_MODE_TRANS_FRE	,(uint8_t*)&manual_mode_trans_fre		,4);
					  app_flash_read(AUTO_MODE_ACQ_FRE			,(uint8_t*)&auto_mode_acq_fre				,4);
						app_flash_read(AUTO_MODE_TRANS_FRE		,(uint8_t*)&auto_mode_trans_fre			,4);
					  app_flash_read(THRESHOLD_LOW					,(uint8_t*)&threshold_low						,4);
					  app_flash_read(THRESHOLD_HIGH				,(uint8_t*)&threshold_high					,4);
						app_flash_read(RADAR_BLIND						,(uint8_t*)&radar_blind							,4);	
					
            app_callback_printf(info->at_in, "+SETING=\"%d\",\"%d\",\"%d\",\"%d\",\"%d\",\"%d\",\"%d\",\"%d\"",operation_mode,manual_mode_acq_fre, manual_mode_trans_fre,auto_mode_acq_fre,auto_mode_trans_fre,threshold_low,threshold_high,radar_blind);
					
					return 1;
				}
    return 1;
}


int app_task_at_test(uint8_t *message, uint16_t size, ATInfo *info)
{
				int buad = 0;

				if (sscanf((char *)message, "AT+TEST=\"%d\"",&buad))
        {
//						if(buad){
//							packet_info_add(0);
//						}else{
//							packet_info_read_old(0);
//							packet_info_next();
//						}

					
            app_callback_printf(info->at_in, "+TEST=\"%d\"\r\nOK\r\n",buad);

            return 1;
        }
    return 1;
}


int app_task_at_relay(uint8_t *message, uint16_t size, ATInfo *info)
{
	
	int channel,state;
	
	int state1,state2,state3,state4;
	// 使用sscanf函数从命令中提取通道号和状态
	if (sscanf((char *)message, "AT+RELAY=%d,%d",&channel,&state)==2)
	{
		
		// 如果成功提取了两个参数，打印结果
			 control_device_jy_dam_relay(0x1F,channel, state);
//			modbus_send_05h(slave_addr,0x0000,0xFF00);
//		control_device_jy_dam_relay( channel, state);
		
		//app_onenet_point_relay("relay",channel,state);
		
	}
	if (sscanf((char *)message, "AT+RELAYTOGGLE=%d",&channel)==1)
	{
		// 如果成功提取了两个参数，打印结果
//		relay_togglePin(channel);
//		
//		uint8_t state;
//		
//		state=is_relay_on(channel);
//		
//		app_onenet_point_relay("relay",channel,state);

	}
	
	if (sscanf((char *)message, "AT+RELAYALL=%d,%d,%d,%d",&state1,&state2,&state3,&state4)==4)
	{
		// 如果成功提取了两个参数，打印结果
		
			
//		 relay_control_all(state1,state2,state3,state4);
//		 
//		 app_onenet_point_relay("relay",1,state1);
//		 app_onenet_point_relay("relay",2,state2);
//		 app_onenet_point_relay("relay",3,state3);
//		 app_onenet_point_relay("relay",4,state4);
//		 
//		 app_callback_printf(info->at_in,"relay state:%d,%d,%d,%d\r\n",is_relay_on(1),is_relay_on(2),is_relay_on(3),is_relay_on(4));
	}
	
	
	
//	if(strstr((char *)message,"AT+RELAY?\r\n"))
//	{
//		 app_callback_printf(info->at_in,"relay number:%d\r\n",4);
//		 app_callback_printf(info->at_in,"relay state:%d,%d,%d,%d\r\n",is_relay_on(1),is_relay_on(2),is_relay_on(3),is_relay_on(4));
//	}
//	
}







int app_task_at_input(uint8_t *message, uint16_t size, ATInfo *info)
{

//	if(strstr((char *)message,"AT+INPUT?\r\n"))
//	{
//		 app_callback_printf(info->at_in,"input number:%d\r\n",4);
//		//输入低电平有效，但是为了直观，这里可以取反
//		 app_callback_printf(info->at_in,"input state:%d,%d,%d,%d\r\n",app_check_input_state(1),app_check_input_state(2),app_check_input_state(3),app_check_input_state(4));
//	}
//	
	
}



int app_task_at_imei(uint8_t *message, uint16_t size, ATInfo *info)
{

				if (strstr((char *)message, "AT+IMEI?"))
        {
            //
						device_imei_info	imei={0};
						
						imei=ec20_app_network_get_imei();
						
						app_callback_printf(info->at_in, "+IMEI=\"%s\"\r\nOK\r\n",imei.imei);
						
            return 1;
        }
    return 1;
}

int app_task_at_clear(uint8_t *message, uint16_t size, ATInfo *info)
{
//		if (strstr((char *)message, "AT+CLEAR\r\n"))
//		{
//				packet_info_clear();
//			
//				app_callback_printf(info->at_in, "+CLEAR=\"1\"\r\nOK\r\n");
//				
//				return 1;
//		}
    return 1;
}


int app_task_at_log(uint8_t *message, uint16_t size, ATInfo *info)
{
				int state = 0;

				if (sscanf((char *)message, "AT+LOG=\"%d\"",&state))
        {
						app_log_control(state);
						app_flash_write(LOG_EN,(uint8_t*)&state,4);
            app_callback_printf(info->at_in, "+LOG=\"%d\"\r\nOK\r\n",state);

            return 1;
        }
    return 1;
}

int app_task_at_relay_uploadset(uint8_t *message, uint16_t size, ATInfo *info)
{

//			int  enable;
//			uint32_t time_set = 0;

//			if (sscanf((char *)message, "AT+RELAYSET=\"%d\"",&time_set))
//      {
//				 app_relay_upload_set(time_set);
//         app_callback_printf(info->at_in, "+RELAYSET=\"%d\",\"%d\"\r\nOK\r\n",enable,time_set);

//         return 1;
//      }
    return 1;
	//void app_relay_upload_set(uint32_t time_set);
}

int app_task_at_topic(uint8_t *message, uint16_t size, ATInfo *info){

		return 1;
}
