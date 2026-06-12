//#ifndef DISPATCHER_EXPORT_H
//#define DISPATCHER_EXPORT_H

//#include <stdint.h>
//#include <stdio.h>

//typedef void (*dispatcher_handler_t)(void);

//typedef struct
//{
//    uint16_t cmd;
//    dispatcher_handler_t handler;
//} dispatcher_entry_t;

//// ×Ô¶¯×¢²áºê
//#define DISPATCHER_REGISTER(cmd_id, func) \
//const dispatcher_entry_t dispatcher_##func \
//__attribute__((used,section("dispatcher"))) = { cmd_id, func };

//#endif