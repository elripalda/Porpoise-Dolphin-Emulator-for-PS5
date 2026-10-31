/* Porpoise - hidapi's interface, as the Dolphin core calls it: hidapi 0.15
 * (Externals/hidapi/hidapi-src/hidapi/hidapi.h in Dolphin's tree; the PC
 * tests compile src/porpoise_usbhid.cpp against that header instead). The
 * functions are porpoise_usbhid.cpp's.
 * Copyright (C) 2026 Ruben (Project Porpoise)
 * SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

#include <stddef.h>
#include <wchar.h>

#ifdef __cplusplus
extern "C" {
#endif

struct hid_device_;
typedef struct hid_device_ hid_device;

typedef enum
{
    HID_API_BUS_UNKNOWN = 0x00,
    HID_API_BUS_USB = 0x01,
    HID_API_BUS_BLUETOOTH = 0x02,
    HID_API_BUS_I2C = 0x03,
    HID_API_BUS_SPI = 0x04,
} hid_bus_type;

struct hid_device_info
{
    char *path;
    unsigned short vendor_id;
    unsigned short product_id;
    wchar_t *serial_number;
    unsigned short release_number;
    wchar_t *manufacturer_string;
    wchar_t *product_string;
    unsigned short usage_page;
    unsigned short usage;
    int interface_number;
    struct hid_device_info *next;
    hid_bus_type bus_type;
};

int hid_init(void);
int hid_exit(void);
struct hid_device_info *hid_enumerate(unsigned short vendor_id, unsigned short product_id);
void hid_free_enumeration(struct hid_device_info *devs);
hid_device *hid_open(unsigned short vendor_id, unsigned short product_id, const wchar_t *serial_number);
hid_device *hid_open_path(const char *path);
int hid_write(hid_device *dev, const unsigned char *data, size_t length);
int hid_read_timeout(hid_device *dev, unsigned char *data, size_t length, int milliseconds);
int hid_read(hid_device *dev, unsigned char *data, size_t length);
int hid_set_nonblocking(hid_device *dev, int nonblock);
int hid_send_feature_report(hid_device *dev, const unsigned char *data, size_t length);
void hid_close(hid_device *dev);
const wchar_t *hid_error(hid_device *dev);

#ifdef __cplusplus
}
#endif
