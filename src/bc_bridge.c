#include "bc_bridge.h"
#include "bc_log.h"
#include <linux/hidraw.h>
#include <linux/input.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <fcntl.h>
#include <libudev.h>
#include <errno.h>
#include <unistd.h>

#define BC_BRIDGE_DEBUG 1

#define REPORT_ID_CHIP_CODE               0xA0
#define REPORT_ID_SYSTEM_SETTING          0xA1
#define REPORT_ID_GPIO                    0xB0
#define REPORT_ID_I2C_STATUS              0xC0
#define REPORT_ID_I2C_READ_REQUEST        0xC2
#define REPORT_ID_UART_STATUS             0xE0
#define BC_BRIDGE_LOG_ENABLE              false
#define BC_BRIDGE_LOG_DISABLE             true
#define BC_BRIDGE_I2C_SWITCH_ADDRESS      0x70
#define BC_BRIDGE_PIN_LED                 0x80
#define BC_BRIDGE_I2C_FLAG_START          0x02
#define BC_BRIDGE_I2C_FLAG_START_AND_STOP 0x06

typedef enum
{
    BC_BRIDGE_I2C_CLOCK_SPEED_100KHZ = 0,
    BC_BRIDGE_I2C_CLOCK_SPEED_400KHZ = 1

} bc_bridge_i2c_clock_speed_t;

static bool _bc_bridge_i2c_set_channel(bc_bridge_t *self, bc_bridge_i2c_channel_t i2c_channel);
static bool _bc_bridge_ft260_i2c_set_clock_speed(bc_bridge_t *self, bc_bridge_i2c_clock_speed_t clock_speed);
static bool _bc_bridge_ft260_get_i2c_bus_status(bc_bridge_t *self, uint8_t *bus_status);
static bool
_bc_bridge_ft260_i2c_write(bc_bridge_t *self, uint8_t device, uint8_t *buffer, uint8_t length, uint8_t flag,
                           bool disable_log);
static bool _bc_bridge_ft260_i2c_read(bc_bridge_t *self, uint8_t device, uint8_t *buffer, uint8_t length);
static bool _bc_bridge_ft260_i2c_reset(bc_bridge_t *self);
static bool _bc_bridge_ft260_get_feature(bc_bridge_t *self, uint8_t *buffer, size_t length);
static bool _bc_bridge_ft260_set_feature(bc_bridge_t *self, uint8_t *buffer, size_t length);
static void _bc_bridge_log_bus_error(char *msg, uint8_t address, uint8_t bus_status);

bool bc_bridge_scan(bc_bridge_device_info_t *devices, uint8_t *device_count)
{
    struct udev *udev;
    struct udev_enumerate *enumerate;
    struct udev_list_entry *entry, *devices_hid;
    uint8_t max_device_count = *device_count;
    uint8_t i;
    char *path;
    char *usb_dev_path;
    char *str_tmp;
    uint16_t id_vendor;
    uint16_t id_product;
    struct udev_device *hid_dev;
    struct udev_device *usb_dev;

    *device_count = 0;

    for (i = 0; i < max_device_count; i++)
    {
        if (devices[i].usb_path != NULL)
        {
            free(devices[i].usb_path);
        }
        devices[i].usb_path = NULL;

        if (devices[i].path_i2c != NULL)
        {
            free(devices[i].path_i2c);
        }
        devices[i].path_i2c = NULL;

        if (devices[i].path_uart != NULL)
        {
            free(devices[i].path_uart);
        }
        devices[i].path_uart = NULL;
    }

    udev = udev_new();

    if (udev == NULL)
    {
        bc_log_error("bc_bridge_scan: call failed: udev_new");

        return false;
    }

    enumerate = udev_enumerate_new(udev);
    udev_enumerate_add_match_subsystem(enumerate, "hidraw");
    udev_enumerate_scan_devices(enumerate);
    devices_hid = udev_enumerate_get_list_entry(enumerate);

    udev_list_entry_foreach(entry, devices_hid)
    {

        path = (char *) udev_list_entry_get_name(entry);
        hid_dev = udev_device_new_from_syspath(udev, path);

        usb_dev = udev_device_get_parent_with_subsystem_devtype(hid_dev, "usb", "usb_device");

        str_tmp = (char *) udev_device_get_sysattr_value(usb_dev, "idVendor");
        id_vendor = (uint16_t) ((str_tmp) ? strtol(str_tmp, NULL, 16) : -1);

        str_tmp = (char *) udev_device_get_sysattr_value(usb_dev, "idProduct");
        id_product = (uint16_t) ((str_tmp) ? strtol(str_tmp, NULL, 16) : -1);

        if (id_vendor == 0x0403 && id_product == 0x6030)
        {
            usb_dev_path = (char *) udev_device_get_devnode(usb_dev);

            for (i = 0; i < max_device_count; i++)
            {
                if ((devices[i].usb_path != NULL) && strcmp(devices[i].usb_path, usb_dev_path) == 0)
                {
                    devices[i].path_uart = strdup(udev_device_get_devnode(hid_dev));
                    break;
                }

            }
            if (i == max_device_count)
            {
                devices[*device_count].usb_path = strdup(usb_dev_path);
                devices[*device_count].path_i2c = strdup(udev_device_get_devnode(hid_dev));

                *device_count += 1;
            }

        }

        udev_device_unref(hid_dev);

    }

    udev_enumerate_unref(enumerate);
    udev_unref(udev);

    return true;
}

bool bc_bridge_open(bc_bridge_t *self, bc_bridge_device_info_t *info)
{
    bc_log_info("bc_bridge_open");

    memset(self, 0, sizeof(*self));

    bc_os_mutex_init(&self->_mutex);

    self->_fd_i2c = open(info->path_i2c, O_RDWR);

    if (self->_fd_i2c == -1)
    {
        bc_log_error("bc_bridge_open: call failed: open");

        return false;
    }

    if (flock(self->_fd_i2c, LOCK_EX | LOCK_NB) == -1)
    {
        bc_log_error("bc_bridge_open: call failed: flock");

        return false;
    }

    if (!_bc_bridge_ft260_i2c_set_clock_speed(self, BC_BRIDGE_I2C_CLOCK_SPEED_400KHZ))
    {
        bc_log_error("bc_bridge_open: call failed: _bc_bridge_ft260_i2c_set_clock_speed");

        return false;
    }

    if (!_bc_bridge_i2c_set_channel(self, BC_BRIDGE_I2C_CHANNEL_1))
    {
        bc_log_error("bc_bridge_open: call failed: _bc_bridge_i2c_set_channel");

        return false;
    }

    return true;
}

bool bc_bridge_close(bc_bridge_t *self)
{
    bc_log_info("bc_bridge_close");

    bc_os_mutex_lock(&self->_mutex);

    if (flock(self->_fd_i2c, LOCK_UN) == -1)
    {
        bc_log_error("bc_bridge_close: call failed: flock");

        bc_os_mutex_unlock(&self->_mutex);

        return false;
    }

    if (close(self->_fd_i2c) == -1)
    {
        bc_log_error("bc_bridge_close: call failed: close");

        bc_os_mutex_unlock(&self->_mutex);

        return false;
    }

    bc_os_mutex_unlock(&self->_mutex);

    bc_os_mutex_destroy(&self->_mutex);

    return true;
}

bool bc_bridge_is_alive(bc_bridge_t *self)
{
    struct stat s;

    if (fstat(self->_fd_i2c, &s) == -1)
    {
        bc_log_error("bc_bridge_is_alive: call failed: fstat");

        return false;
    }

    if (s.st_nlink < 1)
    {
        return false;
    }

    return true;
}

bool bc_bridge_i2c_reset(bc_bridge_t *self)
{
    bc_os_mutex_lock(&self->_mutex);

    bc_log_info("bc_bridge_i2c_reset");

    if (!_bc_bridge_ft260_i2c_reset(self))
    {
        bc_log_error("bc_bridge_i2c_reset: call failed: _bc_bridge_ft260_i2c_reset");

        bc_os_mutex_unlock(&self->_mutex);

        return false;
    }

    bc_os_mutex_unlock(&self->_mutex);

    return true;
}

bool bc_bridge_i2c_write(bc_bridge_t *self, bc_bridge_i2c_transfer_t *transfer)
{
    uint8_t buffer[60];

    bc_os_mutex_lock(&self->_mutex);

    if (transfer->address_16_bit)
    {
        if (transfer->length > 58)
        {
            bc_log_error("bc_bridge_i2c_write: length is too big");

            bc_os_mutex_unlock(&self->_mutex);

            return false;
        }

        buffer[0] = (uint8_t) (transfer->address >> 8);
        buffer[1] = (uint8_t) transfer->address;

        memcpy(&buffer[2], transfer->buffer, transfer->length);
    }
    else
    {
        if (transfer->length > 59)
        {
            bc_log_error("bc_bridge_i2c_write: length is too big");

            bc_os_mutex_unlock(&self->_mutex);

            return false;
        }

        buffer[0] = (uint8_t) transfer->address;

        memcpy(&buffer[1], transfer->buffer, transfer->length);
    }

#if BC_BRIDGE_DEBUG == 1
    if (transfer->address_16_bit)
    {
        bc_log_dump(transfer->buffer, transfer->length,
                    "bc_bridge_i2c_write: channel 0x%02X, device 0x%02X, address 0x%04X, length %d bytes",
                    transfer->channel, transfer->device_address, transfer->address, transfer->length);
    }
    else
    {
        bc_log_dump(transfer->buffer, transfer->length,
                    "bc_bridge_i2c_write: channel 0x%02X, device 0x%02X, address 0x%02X, length %d bytes",
                    transfer->channel, transfer->device_address, transfer->address, transfer->length);
    }
#endif

    if (!_bc_bridge_i2c_set_channel(self, transfer->channel))
    {
        bc_log_error("bc_bridge_i2c_write: call failed: _bc_bridge_i2c_set_channel");

        bc_os_mutex_unlock(&self->_mutex);

        return false;
    }

    if (!_bc_bridge_ft260_i2c_write(self, transfer->device_address, buffer,
                                    (uint8_t) ((transfer->address_16_bit ? 2 : 1) + transfer->length),
                                    BC_BRIDGE_I2C_FLAG_START_AND_STOP, transfer->disable_log))
    {
        if (!transfer->disable_log)
        {
            bc_log_warning(
                "bc_bridge_i2c_write: call failed: _bc_bridge_ft260_i2c_write channel 0x%02X, device 0x%02X, address 0x%02X, length %d bytes",
                transfer->channel, transfer->device_address, transfer->address, transfer->length);
        }

        bc_os_mutex_unlock(&self->_mutex);

        return false;
    }

    bc_os_mutex_unlock(&self->_mutex);

    return true;
}

bool bc_bridge_i2c_read(bc_bridge_t *self, bc_bridge_i2c_transfer_t *transfer)
{
    uint8_t buffer[2];

    if (transfer->length > 60)
    {
        bc_log_error("bc_bridge_i2c_read: length is too big");

        return false;
    }

    bc_os_mutex_lock(&self->_mutex);

#if BC_BRIDGE_DEBUG == 1
    if (transfer->address_16_bit)
    {
        bc_log_dump(NULL, 0, "bc_bridge_i2c_read: channel 0x%02X, device 0x%02X, address 0x%04X, length %d bytes",
                    transfer->channel, transfer->device_address, transfer->address, transfer->length);
    }
    else
    {
        bc_log_dump(NULL, 0, "bc_bridge_i2c_read: channel 0x%02X, device 0x%02X, address 0x%02X, length %d bytes",
                    transfer->channel, transfer->device_address, transfer->address, transfer->length);
    }
#endif

    if (transfer->address != BC_BRIDGE_I2C_ONLY_READ)
    {

        if (transfer->address_16_bit)
        {
            buffer[0] = (uint8_t) (transfer->address >> 8);
            buffer[1] = (uint8_t) transfer->address;
        }
        else
        {
            buffer[0] = (uint8_t) transfer->address;
        }

        if (!_bc_bridge_i2c_set_channel(self, transfer->channel))
        {
            bc_log_error("bc_bridge_i2c_read: call failed: _bc_bridge_i2c_set_channel");

            bc_os_mutex_unlock(&self->_mutex);

            return false;
        }

        if (!_bc_bridge_ft260_i2c_write(self, transfer->device_address, buffer,
                                        (uint8_t) (transfer->address_16_bit ? 2 : 1), BC_BRIDGE_I2C_FLAG_START,
                                        transfer->disable_log))
        {
            if (!transfer->disable_log)
            {
                bc_log_warning(
                    "bc_bridge_i2c_read: call failed: _bc_bridge_ft260_i2c_write channel 0x%02X, device 0x%02X, address 0x%02X",
                    transfer->channel, transfer->device_address, transfer->address);
            }

            bc_os_mutex_unlock(&self->_mutex);

            return false;
        }

    }

    if (!_bc_bridge_ft260_i2c_read(self, transfer->device_address, transfer->buffer,
                                   transfer->length))
    {
        bc_log_warning(
            "bc_bridge_i2c_read: call failed: _bc_bridge_ft260_i2c_read channel 0x%02X, device 0x%02X, length %d bytes",
            transfer->channel, transfer->device_address, transfer->length);

        bc_os_mutex_unlock(&self->_mutex);

        return false;
    }

#if BC_BRIDGE_DEBUG == 1
    bc_log_dump(transfer->buffer, transfer->length, "bc_bridge_i2c_read: read %d bytes", transfer->length);
#endif

    bc_os_mutex_unlock(&self->_mutex);

    return true;
}

bool bc_bridge_i2c_ping(bc_bridge_t *self, bc_bridge_i2c_channel_t channel, uint8_t device_address)
{
    uint8_t buffer[1];
    buffer[0] = 0x00;

    bc_os_mutex_lock(&self->_mutex);

    if (!_bc_bridge_i2c_set_channel(self, channel))
    {
        bc_os_mutex_unlock(&self->_mutex);

        return false;
    }

    bool result = _bc_bridge_ft260_i2c_write(self, device_address, buffer, 1, BC_BRIDGE_I2C_FLAG_START,
                                             BC_BRIDGE_LOG_DISABLE);

    bc_os_mutex_unlock(&self->_mutex);

    return result;
}

bool bc_bridge_led_set_state(bc_bridge_t *self, bc_bridge_led_state_t state)
{
    uint8_t buffer[5] = { 0, 0, 0, 0, 0 };

    bc_os_mutex_lock(&self->_mutex);

    buffer[0] = REPORT_ID_GPIO;

    if (!_bc_bridge_ft260_get_feature(self, buffer, sizeof(buffer)))
    {
        bc_os_mutex_unlock(&self->_mutex);

        return false;
    }

    if (state == BC_BRIDGE_LED_STATE_ON)
    {
        buffer[3] |= BC_BRIDGE_PIN_LED;
    }
    else
    {
        buffer[3] &= ~BC_BRIDGE_PIN_LED;
    }

    /* Set output direction */
    buffer[4] |= BC_BRIDGE_PIN_LED;

    if (!_bc_bridge_ft260_set_feature(self, buffer, sizeof(buffer)))
    {
        bc_os_mutex_unlock(&self->_mutex);

        return false;
    }

    bc_os_mutex_unlock(&self->_mutex);

    return true;
}

bool bc_bridge_led_get_state(bc_bridge_t *self, bc_bridge_led_state_t *state)
{
    uint8_t buffer[5];

    bc_os_mutex_lock(&self->_mutex);

    buffer[0] = REPORT_ID_GPIO;

    if (!_bc_bridge_ft260_get_feature(self, buffer, sizeof(buffer)))
    {
        bc_os_mutex_unlock(&self->_mutex);

        return false;
    }

    *state = (buffer[3] & 0x80) != 0 ? BC_BRIDGE_LED_STATE_ON : BC_BRIDGE_LED_STATE_OFF;

    bc_os_mutex_unlock(&self->_mutex);

    return true;
}

static bool _bc_bridge_i2c_set_channel(bc_bridge_t *self, bc_bridge_i2c_channel_t i2c_channel)
{
    uint8_t buffer[1];

    if (self->_i2c_channel == i2c_channel)
    {
        return true;
    }

    switch (i2c_channel)
    {
        case BC_BRIDGE_I2C_CHANNEL_0:
        {
            buffer[0] = 0x01;
            break;
        }
        case BC_BRIDGE_I2C_CHANNEL_1:
        {
            buffer[0] = 0x02;
            break;
        }
        default:
        {
            return false;
        }
    }

    if (_bc_bridge_ft260_i2c_write(self, BC_BRIDGE_I2C_SWITCH_ADDRESS, buffer, sizeof(buffer),
                                   BC_BRIDGE_I2C_FLAG_START_AND_STOP, BC_BRIDGE_LOG_ENABLE))
    {
        self->_i2c_channel = i2c_channel;

        return true;
    }

    bc_log_error("_bc_bridge_i2c_set_channel: call failed: _bc_bridge_ft260_i2c_write");

    return false;
}

static bool _bc_bridge_ft260_i2c_reset(bc_bridge_t *self)
{
    uint8_t buffer[2];
    uint8_t bus_status;
    bc_tick_t tick_timeout;

    buffer[0] = REPORT_ID_SYSTEM_SETTING;
    buffer[1] = 0x20;

    if (!_bc_bridge_ft260_set_feature(self, buffer, sizeof(buffer)))
    {
        bc_log_error("_bc_bridge_ft260_i2c_reset: call failed: _bc_bridge_ft260_set_feature");

        return false;
    }

    tick_timeout = bc_tick_get() + 1000;

    while (true)
    {
        if (bc_tick_get() >= tick_timeout)
        {
            bc_log_error("_bc_bridge_ft260_i2c_reset: i2c ready timeout bus_status check");

            return false;
        }
        if (!_bc_bridge_ft260_get_i2c_bus_status(self, &bus_status))
        {
            bc_log_error("_bc_bridge_ft260_i2c_reset: call failed: _bc_bridge_ft260_get_i2c_bus_status");

            return false;
        }

        if (bus_status == 0x20)
        {
            return true;
        }

        bc_os_task_sleep(5);
    };

    return false;
}

static bool _bc_bridge_ft260_i2c_set_clock_speed(bc_bridge_t *self, bc_bridge_i2c_clock_speed_t clock_speed)
{
    uint8_t report[4];

    switch (clock_speed)
    {
        case BC_BRIDGE_I2C_CLOCK_SPEED_100KHZ:
        {
            bc_log_debug("_bc_bridge_ft260_i2c_set_clock_speed: requested 100kHz");

            report[0] = REPORT_ID_SYSTEM_SETTING;
            report[1] = 0x22;
            report[2] = 0x64;
            report[3] = 0x00;

            break;
        }
        case BC_BRIDGE_I2C_CLOCK_SPEED_400KHZ:
        {
            bc_log_debug("_bc_bridge_ft260_i2c_set_clock_speed: requested 400kHz");

            report[0] = REPORT_ID_SYSTEM_SETTING;
            report[1] = 0x22;
            report[2] = 0x90;
            report[3] = 0x01;

            break;
        }
        default:
        {
            bc_log_error("_bc_bridge_ft260_i2c_set_clock_speed: speed is not supported");

            return false;
        }
    }

    if (!_bc_bridge_ft260_set_feature(self, report, sizeof(report)))
    {
        bc_log_error("_bc_bridge_ft260_i2c_set_clock_speed: call failed: _bc_bridge_ft260_set_feature");

        return false;
    }

    return true;
}

static bool _bc_bridge_ft260_get_i2c_bus_status(bc_bridge_t *self, uint8_t *bus_status)
{
    uint8_t report[5] = { REPORT_ID_I2C_STATUS, 0, 0, 0, 0 };

    if (!_bc_bridge_ft260_get_feature(self, report, sizeof(report)))
    {
        bc_log_error("_bc_bridge_ft260_get_i2c_bus_status: call failed: _bc_bridge_ft260_get_feature");

        return false;
    }

    *bus_status = report[1];

    return true;
}

static bool
_bc_bridge_ft260_i2c_write(bc_bridge_t *self, uint8_t device, uint8_t *buffer, uint8_t length, uint8_t flag,
                           bool disable_log)
{
    uint8_t report[64];
    uint8_t bus_status = 0;
    bc_tick_t tick_timeout;
    size_t to_send;

    ssize_t res;

    if (length > 60)
    {
        if (!disable_log)
        {
            bc_log_error("_bc_bridge_ft260_i2c_write: length is too big");
        }

        return false;
    }

    memcpy(&report[4], buffer, length);

    report[0] = (uint8_t) (0xD0 + ((length - 1) / 4));
    report[1] = device;
    report[2] = flag;
    report[3] = length;

    to_send = 4 + length;

    if (!bc_bridge_is_alive(self))
    {
        return false;
    }

    while (true)
    {

        res = write(self->_fd_i2c, report, to_send);

        if (res != to_send)
        {

            if (errno == EINTR)
            {
                bc_log_error("_bc_bridge_ft260_i2c_write: call failed: write: EINTR");

                continue;
            }

            if (!disable_log)
            {
                if (res == -1)
                {
                    bc_log_error("_bc_bridge_ft260_i2c_write: call failed: write, device 0x%02X", device);
                }
                else
                {
                    bc_log_error("_bc_bridge_ft260_i2c_write: requested length does not match, device 0x%02X, to_send: %d res: %d", device, to_send, res);
                }

            }

            return false;
        }
        else
        {
            break;
        }

    }

    tick_timeout = bc_tick_get() + 100;

    while (true)
    {
        if (bc_tick_get() >= tick_timeout)
        {
            if (!disable_log)
            {
                bc_log_error("_bc_bridge_ft260_i2c_write: i2c ready timeout bus_status check");
            }

            return false;
        }
        if (!_bc_bridge_ft260_get_i2c_bus_status(self, &bus_status))
        {
            if (!disable_log)
            {
                bc_log_error("_bc_bridge_ft260_i2c_write: call failed: _bc_bridge_ft260_get_i2c_bus_status");
            }

            return false;
        }

        if ((bus_status & 0x01) == 0)
        {
            break;
        }

        bc_os_task_sleep(1);
    };

    if ((bus_status & 0x1E) != 0)
    {
        if (!disable_log)
        {
            _bc_bridge_log_bus_error("_bc_bridge_ft260_i2c_write", device, bus_status);
        }

        return false;
    }

    return true;
}

static bool _bc_bridge_ft260_i2c_read(bc_bridge_t *self, uint8_t device, uint8_t *buffer, uint8_t length)
{
    uint8_t report[64];
    uint8_t bus_status;
    bc_tick_t tick_timeout;
    size_t to_send;

    struct timeval tv;

    ssize_t res;

    fd_set fds;

    if (length > 60)
    {
        bc_log_error("_bc_bridge_ft260_i2c_read: length is too big");

        return false;
    }

    report[0] = REPORT_ID_I2C_READ_REQUEST;
    report[1] = device;
    report[2] = BC_BRIDGE_I2C_FLAG_START_AND_STOP;
    report[3] = length;
    report[4] = 0;

    to_send = 5;

    if (!bc_bridge_is_alive(self))
    {
        return false;
    }

    while (true)
    {

        res = write(self->_fd_i2c, report, to_send);

        if (res != to_send)
        {

            if (errno == EINTR)
            {
                bc_log_error("_bc_bridge_ft260_i2c_read: call failed: write: EINTR");

                continue;
            }

            if (res == -1)
            {
                bc_log_error("_bc_bridge_ft260_i2c_read: call failed: write");
            }
            else
            {
                bc_log_error("_bc_bridge_ft260_i2c_read: requested length does not match, device 0x%02X, to_send: %d res: %d", device, to_send, res);
            }

            return false;
        }
        else
        {
            break;
        }

    }

    tick_timeout = bc_tick_get() + 100;

    while (true)
    {
        if (bc_tick_get() >= tick_timeout)
        {
            bc_log_error("_bc_bridge_ft260_i2c_write: i2c ready timeout bus_status check");

            return false;
        }
        if (!_bc_bridge_ft260_get_i2c_bus_status(self, &bus_status))
        {
            bc_log_error("_bc_bridge_ft260_i2c_write: call failed: _bc_bridge_ft260_get_i2c_bus_status");

            return false;
        }

        if ((bus_status & 0x01) == 0)
        {
            break;
        }

        bc_os_task_sleep(1);
    };

    if ((bus_status & 0x1E) != 0)
    {
        _bc_bridge_log_bus_error("_bc_bridge_ft260_i2c_read", device, bus_status);
        return false;
    }


    tv.tv_sec = 1;
    tv.tv_usec = 0;

    while (true)
    {
        FD_ZERO(&fds);
        FD_SET(self->_fd_i2c, &fds);

        res = select(self->_fd_i2c + 1, &fds, NULL, NULL, &tv);

        if (res == -1)
        {
            if (errno == EINTR)
            {
                bc_log_error("_bc_bridge_ft260_i2c_read: call failed: read: EINTR");

                continue;
            }

            bc_log_error("_bc_bridge_ft260_i2c_read: call failed: select");

            return false;
        }

        if (res == 0)
        {
            bc_log_error("_bc_bridge_ft260_i2c_read: read timeout");

            return false;
        }

        if (read(self->_fd_i2c, report, 64) == -1)
        {
            if (errno == EINTR)
            {
                bc_log_error("_bc_bridge_ft260_i2c_read: call failed: read: EINTR");

                continue;
            }

            bc_log_error("_bc_bridge_ft260_i2c_read: call failed: read, device 0x%02X", device);

            return false;
        }

        break;
    }

    if (length != report[1])
    {
        bc_log_warning("_bc_bridge_ft260_i2c_read: requested length does not match, device 0x%02X, length: %d report[1]: %d", device, length, report[1]);

        return false;
    }

    memcpy(buffer, &report[2], report[1]);

    return true;
}

static bool _bc_bridge_ft260_get_feature(bc_bridge_t *self, uint8_t *buffer, size_t length)
{
    if ((self == NULL) || (self->_fd_i2c == 0) || (buffer == NULL) || (length < 1))
    {
        return false;
    }

    if (!bc_bridge_is_alive(self))
    {
        return false;
    }

    while (true)
    {
        if (ioctl(self->_fd_i2c, HIDIOCGFEATURE(length), buffer) == -1)
        {

            if (errno == EINTR)
            {
                bc_log_error("_bc_bridge_ft260_get_feature: call failed: ioctl: EINTR");

                continue;
            }

            bc_log_error("_bc_bridge_ft260_get_feature: call failed: ioctl");

            return false;
        }

        return true;
    }

}

static bool _bc_bridge_ft260_set_feature(bc_bridge_t *self, uint8_t *buffer, size_t length)
{
    if ((self == NULL) || (self->_fd_i2c == 0))
    {
        return false;
    }

    if (!bc_bridge_is_alive(self))
    {
        return false;
    }

    while (true)
    {
        if (ioctl(self->_fd_i2c, HIDIOCSFEATURE(length), buffer) == -1)
        {

            if (errno == EINTR)
            {
                bc_log_error("_bc_bridge_ft260_set_feature: call failed: ioctl: EINTR");

                continue;
            }

            bc_log_error("_bc_bridge_ft260_set_feature: call failed: ioctl");

            return false;
        }

        return true;
    }

}

static void _bc_bridge_log_bus_error(char *msg, uint8_t address, uint8_t bus_status)
{
    bc_log_error("%s: device 0x%02X: bus error 0x%X", msg, address, bus_status);
    if ((bus_status & 0x01) != 0)
    {
        bc_log_debug("    bit 0 = controller busy: all other status bits invalid");
    }
    if ((bus_status & 0x02) != 0)
    {
        bc_log_debug("    bit 1 = error condition");
    }
    if ((bus_status & 0x04) != 0)
    {
        bc_log_debug("    bit 2 = slave address was not acknowledged during last");
    }
    if ((bus_status & 0x08) != 0)
    {
        bc_log_debug("    bit 3 = data not acknowledged during last operation");
    }
    if ((bus_status & 0x10) != 0)
    {
        bc_log_debug("    bit 4 = arbitration lost during last operation");
    }
    if ((bus_status & 0x20) != 0)
    {
        bc_log_debug("    bit 5 = controller idle");
    }
    if ((bus_status & 0x40) != 0)
    {
        bc_log_debug("    bit 6 = bus busy");
    }

}
