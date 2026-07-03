

#include "cJSON.h"

#include "tlsf.h"

#include "string.h"

#include "log.h"

#include "main.h"

#include "app_gather.h"

//#define MEM_POLL_SIZE	(1024*5)

//uint8_t g_malloc_mem_pool[MEM_POLL_SIZE];

void app_cjson_init(){

// init_memory_pool(MEM_POLL_SIZE,g_malloc_mem_pool);
// 
// log("内存池初始化完毕");
// 
// log("内存池最大容量:%d",get_max_size(g_malloc_mem_pool));
// log("内存池可用容量:%d",get_used_size(g_malloc_mem_pool));


	cJSON_Hooks hooks;
	hooks.free_fn=tlsf_free;
	hooks.malloc_fn=tlsf_malloc;
	
	cJSON_InitHooks(&hooks);
	
}

void cmd_process(char *msg){

//		msg ="{\"method\":\"thing.service.property.set\",\"id\":\"1627877991\",\"params\":{\"relay\":1},\"version\":\"1.0.0\"}";


	cJSON * root =  cJSON_Parse(msg);//这一步是序列化，把字符串，序列化成json结构，也就是生成一个json类，之后就是json类那样处理了，就不是字符串了。

	if(root){ //先要保证 parse 成功
	
		//注意这个root，是最大的节点。
		
		//这个方法，用来获取key，需要填他的父节点，因为json能嵌套
		cJSON * ld = cJSON_GetObjectItemCaseSensitive(root,"id");//获取 key = leduo 的value
		cJSON * lz = cJSON_GetObjectItemCaseSensitive(root,"name");//获取 key = leduo 的value
		cJSON * lzl = cJSON_GetObjectItemCaseSensitive(root,"sex");//获取 key = leduo 的value
		//假设你已经知道他是字符串。
		
		//通过valuexxx 来获取他的值,注意先确认是否为null
		
		if(!cJSON_IsNull(ld) && cJSON_IsString(ld)){ //不为空，且是字符串
		
			log("id:%s",ld->valuestring);
			
		
		}
		
		if(!cJSON_IsNull(lz) && cJSON_IsString(lz)){ //不为空，且是字符串
		
			log("name:%s",lz->valuestring);
		
		}		
		
		if(!cJSON_IsNull(lzl) && cJSON_IsString(lzl)){ //不为空，且是数字
		
			log("sex:%s",lzl->valuestring);//用的value int了哦。
			
//			if(lzl->valueint==28){
//			
//					//modbus_device_jy_do(1,1);
//			}
		
		}
		
		cJSON * param = cJSON_GetObjectItemCaseSensitive(root,"params");//获取 key = leduo 的value
		
		if(!cJSON_IsNull(param) && cJSON_IsObject(param)){
		
			//这里是param 就跟 root 一样，是一个新的json节点
			
			cJSON * relay1 = cJSON_GetObjectItemCaseSensitive(param,"relay1");//注意第一个参数，是他的父节点，relay的父节点是 param，param的父节点是root
		
			if(!cJSON_IsNull(relay1) && cJSON_IsNumber(relay1)){
			
					log("relay1 value:%d",relay1->valueint);
					
					control_device_jy_dam_relay(0x1F,1,relay1->valueint);
					
			}
			
			cJSON * relay2 = cJSON_GetObjectItemCaseSensitive(param,"relay2");//注意第一个参数，是他的父节点，relay的父节点是 param，param的父节点是root
		
			if(!cJSON_IsNull(relay2) && cJSON_IsNumber(relay2)){
			
					log("relay2 value:%d",relay2->valueint);
					
					control_device_jy_dam_relay(0x1F,2,relay2->valueint);
					
			}
			cJSON * relay3 = cJSON_GetObjectItemCaseSensitive(param,"relay3");//注意第一个参数，是他的父节点，relay的父节点是 param，param的父节点是root
		
			if(!cJSON_IsNull(relay3) && cJSON_IsNumber(relay3)){
			
					log("relay3 value:%d",relay3->valueint);
					
					control_device_jy_dam_relay(0x1F,3,relay3->valueint);
					
			}
			
			cJSON * relay4 = cJSON_GetObjectItemCaseSensitive(param,"relay4");//注意第一个参数，是他的父节点，relay的父节点是 param，param的父节点是root
		
			if(!cJSON_IsNull(relay4) && cJSON_IsNumber(relay4)){
			
					log("relay4 value:%d",relay4->valueint);
					
					control_device_jy_dam_relay(0x1F,4,relay4->valueint);
					
			}
		
		}else{
			log("not param");
		}
		
		
		cJSON_Delete(root);//释放整个json结构。
	
	}else{
	
		log("parase error");
	}
		
}