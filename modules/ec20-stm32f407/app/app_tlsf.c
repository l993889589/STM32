#include "tlsf.h"

#include "log.h"


#define MEM_POLL_SIZE	(1024*5)

uint8_t g_malloc_mem_pool[MEM_POLL_SIZE];

void app_tlsf_init(){

 init_memory_pool(MEM_POLL_SIZE,g_malloc_mem_pool);
 
 log("内存池初始化完毕");
 
 log("内存池最大容量:%d",get_max_size(g_malloc_mem_pool));
 log("内存池可用容量:%d",get_used_size(g_malloc_mem_pool));


//	cJSON_Hooks hooks;
//	hooks.free_fn=tlsf_free;
//	hooks.malloc_fn=tlsf_malloc;
//	
//	cJSON_InitHooks(&hooks);
	
}