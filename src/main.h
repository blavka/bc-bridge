#ifndef _MAIN_H
#define _MAIN_H

#include "bc_common.h"
#include "bc_log.h"
#include "bc_bridge.h"

typedef struct
{
    bool furious_mode;
    bc_log_level_t log_level;
    bool dev_list;
    int dev_id;
    char *dev_path;
    char *host;
    int port;
    char *prefix;

} parameters_t;

extern  bc_bridge_t bridge;

#endif //_MAIN_H
