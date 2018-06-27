#include <bc_i2c.h>
#include <main.h>
#include <bc_bridge.h>

void bc_i2c_init(bc_i2c_channel_t channel, bc_i2c_speed_t speed)
{
    (void) speed;
}

bool bc_i2c_write(bc_i2c_channel_t channel, const bc_i2c_tranfer_t *transfer)
{
    bc_bridge_i2c_transfer_t btransfer;
    memset(&btransfer, 0, sizeof(btransfer));

    btransfer.channel = channel == BC_I2C_I2C0 ? BC_BRIDGE_I2C_CHANNEL_0 : BC_BRIDGE_I2C_CHANNEL_1;
    btransfer.device_address = transfer->device_address;
    btransfer.address = (uint16_t)transfer->memory_address;
    btransfer.buffer = transfer->buffer;
    btransfer.length = transfer->length;

    if (!bc_bridge_i2c_write(&bridge, &btransfer))
    {
        return false;
    }

    return true;
}

bool bc_i2c_read(bc_i2c_channel_t channel, const bc_i2c_tranfer_t *transfer)
{
    bc_bridge_i2c_transfer_t btransfer;
    memset(&btransfer, 0, sizeof(btransfer));

    btransfer.channel = channel == BC_I2C_I2C0 ? BC_BRIDGE_I2C_CHANNEL_0 : BC_BRIDGE_I2C_CHANNEL_1;
    btransfer.device_address = transfer->device_address;
    btransfer.address = (uint16_t)transfer->memory_address;
    btransfer.buffer = transfer->buffer;
    btransfer.length = transfer->length;

    if (!bc_bridge_i2c_read(&bridge, &btransfer))
    {
        return false;
    }

    return true;
}

bool bc_i2c_write_8b(bc_i2c_channel_t channel, uint8_t device_address, uint32_t memory_address, uint8_t data)
{
    bc_i2c_tranfer_t transfer;

    transfer.device_address = device_address;
    transfer.memory_address = memory_address;
    transfer.buffer = &data;
    transfer.length = 1;

    return bc_i2c_write(channel, &transfer);
}

bool bc_i2c_write_16b(bc_i2c_channel_t channel, uint8_t device_address, uint32_t memory_address, uint16_t data)
{
    uint8_t buffer[2];

    buffer[0] = data >> 8;
    buffer[1] = data;

    bc_i2c_tranfer_t transfer;

    transfer.device_address = device_address;
    transfer.memory_address = memory_address;
    transfer.buffer = buffer;
    transfer.length = 2;

    return bc_i2c_write(channel, &transfer);
}

bool bc_i2c_read_8b(bc_i2c_channel_t channel, uint8_t device_address, uint32_t memory_address, uint8_t *data)
{
    bc_i2c_tranfer_t transfer;

    transfer.device_address = device_address;
    transfer.memory_address = memory_address;
    transfer.buffer = data;
    transfer.length = 1;

    return bc_i2c_read(channel, &transfer);
}

bool bc_i2c_read_16b(bc_i2c_channel_t channel, uint8_t device_address, uint32_t memory_address, uint16_t *data)
{
    uint8_t buffer[2];

    bc_i2c_tranfer_t transfer;

    transfer.device_address = device_address;
    transfer.memory_address = memory_address;
    transfer.buffer = buffer;
    transfer.length = 2;

    if (!bc_i2c_read(channel, &transfer))
    {
        return false;
    }

    *data = buffer[0] << 8 | buffer[1];

    return true;
}
