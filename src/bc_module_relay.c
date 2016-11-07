#include "bc_module_relay.h"
#include "bc_log.h"

#define BC_MODULE_RELAY_POWER_OFF 0x50
#define BC_MODULE_RELAY_POWER_NO  0xC0
#define BC_MODULE_RELAY_POWER_NC  0x30
#define BC_MODULE_RELAY_DELAY     10

bool bc_module_relay_init(bc_module_relay_t *self, bc_i2c_interface_t *interface, uint8_t device_address)
{
    memset(self, 0, sizeof(*self));

    if (!bc_i2c_tca9534a_init(&self->_tca9534a, interface, device_address))
    {
        bc_log_debug("bc_module_relay_init: call failed: bc_i2c_tca9534a_init");

        return false;
    }

    if (!bc_i2c_tca9534a_write_port(&self->_tca9534a, BC_MODULE_RELAY_POWER_OFF))
    {
        bc_log_error("bc_module_relay_init: call failed: bc_i2c_tca9534a_write_port");

        return false;
    }

    if (!bc_i2c_tca9534a_set_port_direction(&self->_tca9534a, BC_I2C_TCA9534A_DIRECTION_ALL_OUTPUT))
    {
        bc_log_error("bc_module_relay_init: call failed: bc_i2c_tca9534a_set_port_direction");

        return false;
    }

    return true;
}

bool bc_module_relay_set_state(bc_module_relay_t *self, bc_module_relay_state_t state)
{
    uint8_t port;

    switch (state)
    {
        case BC_MODULE_RELAY_STATE_TRUE:
        {
            port = BC_MODULE_RELAY_POWER_NO;
            break;
        }
        case BC_MODULE_RELAY_STATE_FALSE:
        {
            port = BC_MODULE_RELAY_POWER_NC;
            break;
        }

        default:
        {
            return false;
        }
    }

    if (!bc_i2c_tca9534a_write_port(&self->_tca9534a, port))
    {
        bc_log_error("bc_module_relay_set_state: call failed: bc_i2c_tca9534a_write_port");

        return false;
    }

    bc_os_task_sleep(BC_MODULE_RELAY_DELAY);

    if (!bc_i2c_tca9534a_write_port(&self->_tca9534a, BC_MODULE_RELAY_POWER_OFF))
    {
        bc_log_error("bc_module_relay_set_state: call failed: bc_i2c_tca9534a_write_port");

        return false;
    }

    return true;
}
