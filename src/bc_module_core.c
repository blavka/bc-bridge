#include <bc_module_core.h>
#include "bc_os.h"
#include "bc_log.h"
#include <unistd.h>

void bc_module_core_init(void)
{

}

void bc_module_core_sleep()
{
    if (usleep(10UL) != 0)
    {
        bc_log_fatal("bc_os_sleep: call failed: usleep");
    }
}
