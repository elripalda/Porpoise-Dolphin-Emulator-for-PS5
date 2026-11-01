/* Porpoise - FreeBSD's ugen USB ioctls, as the console has them.
 * Copyright (C) 2026 Ruben (Project Porpoise)
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Every USB device on the PS5 has a ugen node (/dev/ugenB.A) that a title
 * may open, with FreeBSD's USB_FS_* transfer table and USB_DO_REQUEST
 * (sys/dev/usb/usb_ioctl.h). The numbers and structures are spelt out here so
 * the PC tests can build them; on the console they're checked against the
 * SDK's header. porpoise_usbhid.cpp (the DolphinBar) keeps its own copy of
 * the same definitions; this one is for the other USB devices Porpoise talks
 * to itself (porpoise_ns2gc.cpp). */
#pragma once

#include <cstdint>

namespace porpoise::ugen
{
constexpr unsigned long kIocOut = 0x40000000UL, kIocIn = 0x80000000UL, kIocInOut = kIocIn | kIocOut;
constexpr unsigned long ioc(unsigned long inout, unsigned long num, unsigned long len)
{
    return inout | ((len & 0x1fffUL) << 16) | (0x55UL /* 'U' */ << 8) | num;
}

struct DeviceRequest /* usb_device_request */
{
    std::uint8_t bmRequestType, bRequest, wValue[2], wIndex[2], wLength[2];
};
struct CtlRequest /* usb_ctl_request */
{
    void *ucr_data;
    std::uint16_t ucr_flags, ucr_actlen;
    std::uint8_t ucr_addr;
    DeviceRequest ucr_request;
} __attribute__((aligned(8)));
struct FsEndpoint /* usb_fs_endpoint */
{
    void **ppBuffer;
    std::uint32_t *pLength;
    std::uint32_t nFrames, aFrames;
    std::uint16_t flags, timeout, isoc_time_complete;
    int status;
} __attribute__((aligned(8)));
struct FsInit /* usb_fs_init */
{
    FsEndpoint *pEndpoints;
    std::uint8_t ep_index_max;
} __attribute__((aligned(8)));
struct FsOpen /* usb_fs_open */
{
    std::uint32_t max_bufsize, max_frames;
    std::uint16_t max_packet_length;
    std::uint8_t dev_index, ep_index, ep_no;
} __attribute__((aligned(4)));
struct FsIndex /* usb_fs_start, _stop, _complete, _close, _uninit */
{
    std::uint8_t ep_index;
};
struct GenDescriptor /* usb_gen_descriptor */
{
    void *ugd_data;
    std::uint16_t ugd_lang_id, ugd_maxlen, ugd_actlen, ugd_offset;
    std::uint8_t ugd_config_index, ugd_string_index, ugd_iface_index, ugd_altif_index, ugd_endpt_index,
        ugd_report_type;
    std::uint8_t reserved[8];
} __attribute__((aligned(8)));
/* usb_device_info: the fields we read. The console's is 8 bytes longer than
 * FreeBSD's 0x170 (BlueV, platform/ps5/ps5_usb_directory.cpp): that number is
 * tried first, then FreeBSD's USB_GET_DEVICEINFO. */
struct DeviceInfo
{
    std::uint16_t productNo, vendorNo, releaseNo, power;
    std::uint8_t bus, addr, index, cls, subclass, protocol, config_no, config_index, speed, mode, nports,
        hubaddr, hubindex, hubport, power_mode, suspended;
    std::uint8_t reserved[16];
    char product[128], vendor[128], serial[64], release[8];
    std::uint8_t ps5_more[8];
};
static_assert(sizeof(DeviceRequest) == 8 && sizeof(CtlRequest) == 24 && sizeof(FsEndpoint) == 40 &&
              sizeof(FsInit) == 16 && sizeof(FsOpen) == 16 && sizeof(GenDescriptor) == 32 &&
              sizeof(DeviceInfo) == 0x178);

constexpr unsigned long kFsStart = ioc(kIocIn, 192, 1), kFsStop = ioc(kIocIn, 193, 1),
                        kFsComplete = ioc(kIocOut, 194, 1), kFsInit = ioc(kIocIn, 195, sizeof(FsInit)),
                        kFsUninit = ioc(kIocIn, 196, 1), kFsOpen = ioc(kIocInOut, 197, sizeof(FsOpen)),
                        kFsClose = ioc(kIocIn, 198, 1), kDoRequest = ioc(kIocInOut, 111, sizeof(CtlRequest)),
                        kGetFullDesc = ioc(kIocInOut, 109, sizeof(GenDescriptor)),
                        kDeviceInfoPs5 = ioc(kIocInOut, 4, 0x178), kDeviceInfo = ioc(kIocOut, 112, 0x170),
                        kIfaceDriverActive = ioc(kIocIn, 124, sizeof(int)),
                        kIfaceDriverDetach = ioc(kIocIn, 125, sizeof(int));
static_assert(kDeviceInfoPs5 == 0xc1785504UL, "BlueV's number");

/* usb_fs_endpoint flags, and the status of a transfer that was stopped. */
constexpr std::uint16_t kFlagSingleShortOk = 0x0001, kFlagForceShort = 0x0004, kFlagClearStall = 0x0008;
constexpr int kErrCancelled = 5;
} // namespace porpoise::ugen

#if !defined(PORPOISE_DESKTOP) && !defined(PORPOISE_USBHID_HOST_TEST) && !defined(PORPOISE_NS2GC_HOST_TEST) && \
    __has_include(<dev/usb/usb_ioctl.h>)
#include <dev/usb/usb.h>
#include <dev/usb/usb_ioctl.h>
static_assert(porpoise::ugen::kFsStart == USB_FS_START && porpoise::ugen::kFsStop == USB_FS_STOP &&
                  porpoise::ugen::kFsComplete == USB_FS_COMPLETE && porpoise::ugen::kFsInit == USB_FS_INIT &&
                  porpoise::ugen::kFsUninit == USB_FS_UNINIT && porpoise::ugen::kFsOpen == USB_FS_OPEN &&
                  porpoise::ugen::kFsClose == USB_FS_CLOSE && porpoise::ugen::kDoRequest == USB_DO_REQUEST &&
                  porpoise::ugen::kGetFullDesc == USB_GET_FULL_DESC &&
                  porpoise::ugen::kDeviceInfo == USB_GET_DEVICEINFO &&
                  porpoise::ugen::kIfaceDriverActive == USB_IFACE_DRIVER_ACTIVE &&
                  porpoise::ugen::kIfaceDriverDetach == USB_IFACE_DRIVER_DETACH,
              "FreeBSD's USB ioctls as spelt out here");
#endif
