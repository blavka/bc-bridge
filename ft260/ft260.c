#include <libudev.h>
#include <linux/hidraw.h>
#include <linux/input.h>
#include <linux/types.h>
#include <string.h>
/*
* For the systems that don't have the new version of hidraw.h in userspace.
*/
#ifndef HIDIOCSFEATURE
#warning Please have your distro update the userspace kernel headers
#define HIDIOCSFEATURE(len) _IOC(_IOC_WRITE | _IOC_READ, 'H', 0x06, len) //LIBUSB_REQUEST_GET_DESCRIPTOR
#define HIDIOCGFEATURE(len) _IOC(_IOC_WRITE | _IOC_READ, 'H', 0x07, len) //LIBUSB_REQUEST_SET_DESCRIPTOR
#endif
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "ft260.h"

int hid_i2c;
int hid_uart;

int ft260_open_device() {

    struct udev *udev;
    struct udev_enumerate *enumerate, *enumerate_hid;
    struct udev_list_entry *devices, *entry, *devices_hid, *entry_hid;
    struct udev_device *dev;
    struct udev_device *usb_dev;
    struct udev_device* hid = NULL;

    struct udev_device *intf_dev;
    char *ret_path = NULL;
    /* Create the udev object */
    udev = udev_new();
    if (!udev) {
        perror("Can't create udev\n");
        return 0;
    }
  
    //enumerate over usb device
    enumerate = udev_enumerate_new(udev);
    udev_enumerate_add_match_subsystem(enumerate, "usb");
    udev_enumerate_add_match_property(enumerate, "DEVTYPE", "usb_device");
    udev_enumerate_scan_devices(enumerate);
    devices = udev_enumerate_get_list_entry(enumerate);

    udev_list_entry_foreach(entry, devices) {
        const char *str;
        unsigned short cur_vid;
        unsigned short cur_did;
        const char* path = udev_list_entry_get_name(entry);

        usb_dev = udev_device_new_from_syspath(udev, path);
    
        str = udev_device_get_sysattr_value(usb_dev, "idVendor");
        cur_vid = (str) ? strtol(str, NULL, 16) : -1;
        str = udev_device_get_sysattr_value(usb_dev, "idProduct");
        cur_did = (str) ? strtol(str, NULL, 16) : -1;

        if (cur_vid == VENDOR_ID && cur_did == DEVICE_ID){
            fprintf(stderr,"path %s \n", udev_device_get_devnode(usb_dev) );

            fprintf(stderr,"idVendor %hx \n", cur_vid );
            fprintf(stderr,"idProduct %hx \n", cur_did );

            enumerate_hid = udev_enumerate_new(udev);
            udev_enumerate_add_match_parent(enumerate_hid, usb_dev);
            udev_enumerate_add_match_subsystem(enumerate_hid, "hidraw");
            udev_enumerate_scan_devices(enumerate_hid);
            devices_hid = udev_enumerate_get_list_entry(enumerate_hid);
            udev_list_entry_foreach(entry_hid, devices_hid) {
                const char *path = udev_list_entry_get_name(entry_hid);
                hid = udev_device_new_from_syspath(udev, path);
                const char *hid_path = udev_device_get_devnode(hid);
                fprintf(stderr,"hid_path %s \n", hid_path );

                if(!hid_i2c){
                    hid_i2c = open(hid_path, O_RDWR | O_NONBLOCK);
                    if (hid_i2c < 0) {
                        perror("Unable to open hid0 ");
                        return -1;
                    }
                }else if(!hid_uart){
                    hid_uart = open(hid_path, O_RDWR | O_NONBLOCK);
                    if (hid_uart < 0) {
                        perror("Unable to open hid1 ");
                        return -1;
                    }
                }else{
                    perror("Unexpected hid device");
                    return -1;
                }
            }
            return hid_i2c>-1 && hid_uart>-1;
        }

    }

    return 0;
}


void ft260_close_dev(){
    close(hid_i2c);
    close(hid_uart);
}

void print_buf(char* buf, int res){
    printf("print_buf res: %d\n", res);
    for (int i = 0; i < res; i++){
        printf("%d %hhx %d\n", i, buf[i], buf[i]);
    }
}

int get_feature(int hid, char* buf, size_t length){
    return ioctl(hid, HIDIOCGFEATURE(length), buf);
}

int set_feature(int hid, char* buf, size_t length){
    return ioctl(hid, HIDIOCSFEATURE(length), buf);
}

int ft260_check_chip_version(){
    char buf[13];
    buf[0] = REPORT_ID_CHIP_CODE;
    int res = get_feature( hid_i2c, buf, sizeof(buf) );
    if (res < 0) {
        return 0;
    }
    return (buf[1]==0x02) && (buf[2]==0x60);
}

void ft260_led(int state){
    char buf[5];
    buf[0] = REPORT_ID_GPIO;
    int res = get_feature( hid_i2c, buf, sizeof(buf) );
    //print_buf(buf, res);
    state = state ? 1: 0;
    buf[3] = (state << 7);
    buf[4] |= 0x80;
    //print_buf(buf, res);
    set_feature(hid_i2c, buf, sizeof(buf));
}

// ------------- i2c --------------------
void ft260_i2c_reset(){
    char buf[] = {REPORT_ID_SYSTEM_SETTING, 0x20};
    set_feature(hid_i2c, buf, sizeof(buf));
    sleep(1);
}

int ft260_i2c_set_clock_speed(int speed){
    if( (speed<60) || (speed>3400))
        return 0;
    char buf[] = {REPORT_ID_SYSTEM_SETTING, 0x22, (char)speed, (char)(speed>>8) };
    return set_feature(hid_i2c, buf, sizeof(buf));
}

int ft260_i2c_get_clock_speed(){
    unsigned char buf[5];
    buf[0] = REPORT_ID_I2C_STATUS;
    int res = get_feature(hid_i2c, buf, sizeof(buf));
    if( (res==5) && buf[0] == REPORT_ID_I2C_STATUS ){
        return (int)( buf[2] | (buf[3]<<8) );
    }
    return -1;
}

int ft260_i2c_write(unsigned char address, char *data, char data_length){
    char buf[ 4+data_length ];
    memcpy(buf+4,data,data_length);
    buf[0] = 0xD0 + ((data_length-1) / 4); /* I2C write */
    buf[1] = address; /* Slave address */
    buf[2] = 0x06; /* Start and Stop */
    buf[3] = data_length;
    return write(hid_i2c, buf, sizeof(buf));
}

int ft260_i2c_read(unsigned char address, char *data, char data_length){
    char buf[64];
    buf[0] = 0xC2; /* I2C write */
    buf[1] = address; /* Slave address */
    buf[2] = 0x06; /* Start and Stop */
    buf[3] = data_length;
    buf[4] = 0;
    int res = write(hid_i2c, buf, 5);
    if(res<0){
        return res;
    }

    fd_set set;
    struct timeval timeout;
    FD_ZERO(&set); /* clear the set */
    FD_SET(hid_i2c, &set); /* add our file descriptor to the set */
    timeout.tv_sec = 0;
    timeout.tv_usec = 500000;
    res = select(hid_i2c + 1, &set, NULL, NULL, &timeout);
    if(res == -1)/* an error accured */
        return -1;
    else if(res == 0)/* a timeout occured */
        return -1;
    else
        res = read(hid_i2c, buf, 64);
        
    if(res<0){
        return res;
    }    
    if( data_length > buf[1] ){
        data_length = buf[1];
    }
    memcpy(data,buf+2,data_length);
    return data_length;
}

void ft260_i2c_set_bus(enum I2C_BUS bus){
    unsigned char data[1];
    data[0] = bus;
    ft260_i2c_write(112, data, sizeof(data));
}

int ft260_i2c_check_device_exist(unsigned char address){
    unsigned char buf[4];
    int res = ft260_i2c_read(address, buf, 4);
    return ( (res==4) && !( (buf[0]==0xFF) && (buf[1]==0xFF) && (buf[2]==0xFF) && (buf[3]==0xFF) ) );
}

void ft260_i2c_scan(){
    for(unsigned char address=1; address<128; address++){
        if( ft260_i2c_check_device_exist(address) ){
             printf("address:  %hhx %d \n", address, address );
        }
       
    }
}

// ------------- uart --------------------

int ft260_uart_reset(){
    char buf[] = {REPORT_ID_SYSTEM_SETTING, 0x40};
    set_feature(hid_uart, buf, sizeof(buf));
    sleep(1);
}

unsigned char* get_uart_status(){
    static unsigned char buf[10];
    buf[0] = REPORT_ID_UART_STATUS;
    get_feature(hid_uart, buf, sizeof(buf));
    return buf;
}

// int ft260_uart_set_clock_speed(int speed){
//     if( (speed<60) || (speed>3400))
//         return 0;
//     char buf[] = {REPORT_ID_SYSTEM_SETTING, 0x22, (char)speed, (char)(speed>>8) };
//     return set_feature(hid_i2c, buf, sizeof(buf));
// }

int ft260_uart_get_flow_ctrl(){
    unsigned char *buf = get_uart_status();
    return buf[1];  
}

int ft260_uart_get_baud_rate(){
    unsigned char *buf = get_uart_status();
    return (int)( buf[2] | (buf[3]<<8) | (buf[4]<<16) | (buf[5]<<24) );
}

int ft260_uart_get_data_bit(){
    unsigned char *buf = get_uart_status();
    return buf[6];
}

int ft260_uart_get_parity(){
    unsigned char *buf = get_uart_status();
    return buf[7];
}

int ft260_uart_get_stop_bit(){
    unsigned char *buf = get_uart_status();
    return buf[8];
}

int ft260_uart_get_breaking(){
    unsigned char *buf = get_uart_status();
    return buf[9];
}

int ft260_uart_write(char *data, char data_length){
    uint8_t buf[ 2+data_length ];
    memcpy(buf+4,data,data_length);
    buf[0] = 0xF0 + ((data_length-1) / 4); 
    buf[1] = data_length;
    return write(hid_uart, buf, sizeof(buf));
}

int ft260_uart_read(char *data, char data_length){
    char buf[64];
    int res;
    fd_set set;
    struct timeval timeval_timeout;
    FD_ZERO(&set); /* clear the set */
    FD_SET(hid_uart, &set); /* add our file descriptor to the set */
    timeval_timeout.tv_sec = 0;
    timeval_timeout.tv_usec = 500000;
    res = select(hid_uart + 1, &set, NULL, NULL, &timeval_timeout);
    if(res == -1)/* an error accured */
        return -1;
    else if(res == 0)/* a timeout occured */
        return -1;
    else
        res = read(hid_uart, buf, 64);
        
    if(res<0){
        return res;
    }    
    //print_buf(buf, res);

    if( data_length > buf[1] ){
        data_length = buf[1];
    }
    memcpy(data,buf+2,data_length);
    return data_length;
}