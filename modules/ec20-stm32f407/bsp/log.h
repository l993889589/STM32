#ifndef _LOG_H_
#define _LOG_H_


#include "bsp_usart.h"
 
#define ANSI_COLOR_RED     "\x1b[31m"
#define ANSI_COLOR_GREEN   "\x1b[32m"
#define ANSI_COLOR_YELLOW  "\x1b[33m"
#define ANSI_COLOR_BLUE    "\x1b[34m"
#define ANSI_COLOR_MAGENTA "\x1b[35m"
#define ANSI_COLOR_CYAN    "\x1b[36m"
#define ANSI_COLOR_RESET   "\x1b[0m"

#if(0)
#define  log(format,...)   debug_printf(ANSI_COLOR_RED"[log][%s][%s][%d] "format""ANSI_COLOR_RED"\r\n",__FILE__,__func__,__LINE__, ##__VA_ARGS__)
#else
#define  log(format,...)   debug_printf("[log][%s][%s][%d] "format"""\r\n",__FILE__,__func__,__LINE__, ##__VA_ARGS__)
#define  log_ex(format,...) debug_printf_no_block("[log][%s][%s][%d] "format"""\r\n",__FILE__,__func__,__LINE__, ##__VA_ARGS__)
#endif
#endif
