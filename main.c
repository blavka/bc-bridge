#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include "bc/bridge.h"
#include <jsmn.h>
#include "tags.h"


tags_data_t data;
bc_tag_temperature_t tag_temperature;
bc_tag_humidity_t tag_humidity;

int main (int argc, char *argv[])
{

    fprintf(stderr,"Bridge build %s \n", VERSION );
    
    bc_bridge_device_info_t devices[6];//TODO predelat na dinamicke pole

    uint8_t length;
    bc_bridge_t bridge;

    bc_bridge_scan(devices, &length);

    fprintf(stderr,"found devices %d \n", length );

    if (length==0)
    {
        fprintf(stderr,"no found any devices, exit \n");
        return EXIT_SUCCESS;
    }

    if (!bc_bridge_open(&bridge, &devices[0]))
    {
        return EXIT_FAILURE;
    }

    bc_tag_interface_t tag_i2c0_interface = {
            .channel = BC_BRIDGE_I2C_CHANNEL_0,
            .bridge = &bridge
    };

    bc_tag_humidity_init(&tag_humidity, &tag_i2c0_interface);

    while(1){

        tags_humidity_task(&tag_humidity, &data);
        if (!data.null)
        {
            printf("[\"humidity-sensor\", {\"0/relative-humidity\":[%f, \"%%\"]}]\n", data.value );
        }
        sleep(1);
        //printf("------------------------------------\n");
    }


    return EXIT_SUCCESS;
}
