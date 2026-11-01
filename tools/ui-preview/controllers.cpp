/* Porpoise UI preview: Controllers & Accessories (ui_app_controllers.cpp),
 * rendered offscreen (any Vulkan driver, e.g. lavapipe) with DolphinBar
 * states set by hand, to check the layout without a console. Generated from
 * preview.cpp's Vulkan set-up by tools/ui-preview/build-controllers.sh.
 *
 *   controllers <assets dir> <covers dir> <out dir>
 */
#include "porpoise_dolphinbar.hpp"
#include "porpoise_ns2gc.hpp"
#include "porpoise_bt.hpp"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <chrono>
#include <thread>
#include <vector>
#include <unistd.h>

#include <sys/stat.h>

#include "porpoise_borders.hpp"
#include "porpoise_states.hpp"
#include "porpoise_banner.hpp"
#include "ui_recommend.hpp"
#include "ui_setups.hpp"
#include "ui_app.hpp"
#include "ui_widescreen.hpp"
#include "ui_i18n.hpp"
#include "ui_gfx.hpp"
#include "ui_library.hpp"
#include "ui_settings.hpp"
#include "porpoise_netfs.hpp"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb/stb_image_write.h"

extern "C" VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetInstanceProcAddr(VkInstance, const char *);

using namespace porpoise::ui;

#define GI(name) auto name = reinterpret_cast<PFN_##name>(vkGetInstanceProcAddr(instance, #name))
#define GD(name) auto name = reinterpret_cast<PFN_##name>(gdpa(device, #name))

int main(int argc, char **argv)
{
    if (argc < 4)
    {
        std::fprintf(stderr, "usage: preview <assets> <covers> <out>\n");
        return 2;
    }
    const std::string assets = argv[1], covers = argv[2], out = argv[3];
    const std::string saves = argc > 4 ? argv[4] : out;
    const unsigned W = 1920, H = 1080;

    VkInstance instance = VK_NULL_HANDLE;
    auto vkCreateInstance = reinterpret_cast<PFN_vkCreateInstance>(vkGetInstanceProcAddr(nullptr, "vkCreateInstance"));
    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.apiVersion = VK_API_VERSION_1_1;
    VkInstanceCreateInfo ici{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    ici.pApplicationInfo = &app;
    if (vkCreateInstance(&ici, nullptr, &instance) != VK_SUCCESS)
        return 1;
    GI(vkEnumeratePhysicalDevices);
    GI(vkCreateDevice);
    GI(vkGetDeviceProcAddr);
    GI(vkGetPhysicalDeviceMemoryProperties);
    VkPhysicalDevice gpu = VK_NULL_HANDLE;
    std::uint32_t n = 1;
    vkEnumeratePhysicalDevices(instance, &n, &gpu);
    const float prio = 1;
    VkDeviceQueueCreateInfo q{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    q.queueCount = 1;
    q.pQueuePriorities = &prio;
    VkDeviceCreateInfo dci{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    dci.queueCreateInfoCount = 1;
    dci.pQueueCreateInfos = &q;
    VkDevice device = VK_NULL_HANDLE;
    if (vkCreateDevice(gpu, &dci, nullptr, &device) != VK_SUCCESS)
        return 1;
    PFN_vkGetDeviceProcAddr gdpa = vkGetDeviceProcAddr;
    GD(vkGetDeviceQueue);
    GD(vkCreateImage);
    GD(vkGetImageMemoryRequirements);
    GD(vkAllocateMemory);
    GD(vkBindImageMemory);
    GD(vkCreateImageView);
    GD(vkCreateRenderPass);
    GD(vkCreateFramebuffer);
    GD(vkCreateCommandPool);
    GD(vkAllocateCommandBuffers);
    GD(vkBeginCommandBuffer);
    GD(vkEndCommandBuffer);
    GD(vkCmdBeginRenderPass);
    GD(vkCmdEndRenderPass);
    GD(vkCmdCopyImageToBuffer);
    GD(vkQueueSubmit);
    GD(vkQueueWaitIdle);
    GD(vkCreateBuffer);
    GD(vkGetBufferMemoryRequirements);
    GD(vkBindBufferMemory);
    GD(vkMapMemory);
    GD(vkResetCommandBuffer);
    VkQueue queue = VK_NULL_HANDLE;
    vkGetDeviceQueue(device, 0, 0, &queue);

    VkPhysicalDeviceMemoryProperties mp{};
    vkGetPhysicalDeviceMemoryProperties(gpu, &mp);
    auto memtype = [&](std::uint32_t bits, VkMemoryPropertyFlags f) {
        for (std::uint32_t i = 0; i < mp.memoryTypeCount; ++i)
            if ((bits & (1u << i)) && (mp.memoryTypes[i].propertyFlags & f) == f)
                return i;
        return 0u;
    };

    const VkFormat format = VK_FORMAT_R8G8B8A8_UNORM;
    VkImageCreateInfo ii{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    ii.imageType = VK_IMAGE_TYPE_2D;
    ii.format = format;
    ii.extent = {W, H, 1};
    ii.mipLevels = ii.arrayLayers = 1;
    ii.samples = VK_SAMPLE_COUNT_1_BIT;
    ii.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    VkImage image;
    vkCreateImage(device, &ii, nullptr, &image);
    VkMemoryRequirements req;
    vkGetImageMemoryRequirements(device, image, &req);
    VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    ai.allocationSize = req.size;
    ai.memoryTypeIndex = memtype(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    VkDeviceMemory imem;
    vkAllocateMemory(device, &ai, nullptr, &imem);
    vkBindImageMemory(device, image, imem, 0);
    VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    vi.image = image;
    vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vi.format = format;
    vi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    VkImageView view;
    vkCreateImageView(device, &vi, nullptr, &view);

    VkAttachmentDescription att{};
    att.format = format;
    att.samples = VK_SAMPLE_COUNT_1_BIT;
    att.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    att.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    att.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    att.finalLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    VkAttachmentReference ref{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkSubpassDescription sub{};
    sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    sub.colorAttachmentCount = 1;
    sub.pColorAttachments = &ref;
    VkRenderPassCreateInfo rpi{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
    rpi.attachmentCount = 1;
    rpi.pAttachments = &att;
    rpi.subpassCount = 1;
    rpi.pSubpasses = &sub;
    VkRenderPass rp;
    vkCreateRenderPass(device, &rpi, nullptr, &rp);
    VkFramebufferCreateInfo fbi{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
    fbi.renderPass = rp;
    fbi.attachmentCount = 1;
    fbi.pAttachments = &view;
    fbi.width = W;
    fbi.height = H;
    fbi.layers = 1;
    VkFramebuffer fb;
    vkCreateFramebuffer(device, &fbi, nullptr, &fb);

    VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bi.size = VkDeviceSize(W) * H * 4;
    bi.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    VkBuffer readback;
    vkCreateBuffer(device, &bi, nullptr, &readback);
    vkGetBufferMemoryRequirements(device, readback, &req);
    ai.allocationSize = req.size;
    ai.memoryTypeIndex = memtype(req.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                                         VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    VkDeviceMemory rmem;
    vkAllocateMemory(device, &ai, nullptr, &rmem);
    vkBindBufferMemory(device, readback, rmem, 0);
    void *mapped = nullptr;
    vkMapMemory(device, rmem, 0, VK_WHOLE_SIZE, 0, &mapped);

    VkCommandPoolCreateInfo cpi{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    cpi.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    VkCommandPool pool;
    vkCreateCommandPool(device, &cpi, nullptr, &pool);
    VkCommandBufferAllocateInfo cai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    cai.commandPool = pool;
    cai.commandBufferCount = 1;
    VkCommandBuffer cmd;
    vkAllocateCommandBuffers(device, &cai, &cmd);

    GfxInit gi;
    gi.instance = instance;
    gi.gpu = gpu;
    gi.device = device;
    gi.queue = queue;
    gi.queue_family = 0;
    gi.render_pass = rp;
    gi.slots = 1;
    gi.get_instance_proc = vkGetInstanceProcAddr;
    gi.get_device_proc = gdpa;
    gi.asset_dir = assets;
    gi.cjk_also = porpoise::ui::language_names();
    if (const char *cache = std::getenv("PREVIEW_CACHE"))
        gi.cache_dir = cache; /* the baked text atlases, as Porpoise keeps them */
    Gfx gfx;
    /* Covers decode on a worker thread in Porpoise; here, in place, so every
     * scene shows them (PREVIEW_ASYNC=1 to see the worker's way). */
    const bool async = std::getenv("PREVIEW_ASYNC") != nullptr;
    gfx.set_async_loads(async);
    if (!gfx.init(gi))
    {
        std::fprintf(stderr, "gfx init failed\n");
        return 1;
    }


    Library lib;
    porpoise::Settings settings;
    App ui;
    ui.init(&gfx, &lib, &settings, out + "/settings.ini", out + "/options.ini", saves);
    auto render = [&](const char *name, auto &&draw) {
        gfx.begin(0, float(W), float(H), 12.0f, 0.0f, false);
        draw();
        vkResetCommandBuffer(cmd, 0);
        VkCommandBufferBeginInfo b{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        vkBeginCommandBuffer(cmd, &b);
        gfx.record_uploads(cmd); /* streamed textures (Wii banners) */
        VkClearValue clear{};
        VkRenderPassBeginInfo rbi{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
        rbi.renderPass = rp;
        rbi.framebuffer = fb;
        rbi.renderArea.extent = {W, H};
        rbi.clearValueCount = 1;
        rbi.pClearValues = &clear;
        vkCmdBeginRenderPass(cmd, &rbi, VK_SUBPASS_CONTENTS_INLINE);
        gfx.record(cmd);
        vkCmdEndRenderPass(cmd);
        VkBufferImageCopy copy{};
        copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        copy.imageExtent = {W, H, 1};
        vkCmdCopyImageToBuffer(cmd, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, readback, 1, &copy);
        vkEndCommandBuffer(cmd);
        VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
        si.commandBufferCount = 1;
        si.pCommandBuffers = &cmd;
        vkQueueSubmit(queue, 1, &si, VK_NULL_HANDLE);
        vkQueueWaitIdle(queue);
        auto *px = static_cast<unsigned char *>(mapped);
        for (std::size_t i = 0; i < std::size_t(W) * H; ++i)
            px[i * 4 + 3] = 255;
        const std::string path = out + "/" + name + ".png";
        stbi_write_png(path.c_str(), int(W), int(H), 4, px, int(W * 4));
        std::printf("wrote %s\n", path.c_str());
    };

    namespace db = porpoise::dolphinbar;
    Input none;
    auto frames = [&](int n) {
        for (int i = 0; i < n; ++i)
            ui.update(none, 0.016);
    };
    auto shot = [&](const char *name) {
        frames(40);
        render(name, [&] { ui.draw(12.0); });
    };
    porpoise::ui::apply_language(1, ""); /* English */
    db::Status s;
    db::preview_status(&s);

    s.monitoring = true;
    ui.preview_controllers(1, false);
    shot("ctl-bar-not-found");

    s.unreadable = {{"ugen1.3", 13}};
    shot("ctl-bar-access-refused");
    s.unreadable.clear();

    s.bar = db::BarState::Ready;
    s.node = "ugen1.3";
    s.vid = 0x057e;
    s.pid = 0x0306;
    s.product = "Nintendo RVL-CNT-01";
    s.descriptors_read = true;
    s.interfaces = {"0|81|00|1|1|5321|812|0|0|0|0|0.0|0.3|3|18|1", "1|82|00|0|1|4410|640|0|0|0|0|0.0|0.4|0|9|0",
                    "2|83|00|0|1|0|0|14|0|0|0|-1.0|-1.0|0|2|0", "3|84|00|0|1|0|0|14|0|0|0|-1.0|-1.0|0|2|0"};
    s.kernel_driver = {1, 0, 0, 0};
    for (int i = 0; i < 4; ++i)
    {
        s.remote[i].state = db::SlotState::NoRemote;
        s.remote[i].interface_number = i;
    }
    db::Remote &a = s.remote[0];
    a.state = db::SlotState::Connected;
    a.have_status = true;
    a.battery_percent = 84;
    a.reports = 5321;
    a.reporting = true;
    a.buttons = db::RbA | db::RbRight;
    a.calibrated = true;
    a.accel_g[0] = 0.12f;
    a.accel_g[1] = -0.31f;
    a.accel_g[2] = 0.94f;
    a.pitch = -18;
    a.roll = 7;
    a.ir_on = true;
    a.ir[0] = {true, 420, 380};
    a.ir[1] = {true, 610, 392};
    a.ext = db::Extension::MotionPlusNunchuk;
    a.motion_plus_seen = true;
    a.ext_reports = 2600;
    a.mp_reports = 2600;
    a.nc_stick[0] = 190;
    a.nc_stick[1] = 96;
    a.nc_c = true;
    a.nc_calibrated = true;
    a.nc_accel_g[0] = -0.05f;
    a.nc_accel_g[1] = 0.02f;
    a.nc_accel_g[2] = 1.01f;
    a.mp_dps[0] = 120;
    a.mp_dps[1] = -35;
    a.mp_dps[2] = 8;
    db::Remote &b = s.remote[1];
    b.state = db::SlotState::Connected;
    b.have_status = true;
    b.battery_percent = 12;
    b.battery_low = true;
    b.reports = 4410;
    b.reporting = true;
    b.ir_on = true;
    b.ext = db::Extension::Classic;
    b.ext_reports = 4000;
    b.cc_buttons = db::CbB | db::CbZR | db::CbLeft;
    b.cc_left[0] = 50;
    b.cc_left[1] = 40;
    b.cc_right[0] = 8;
    b.cc_right[1] = 20;
    b.cc_lt = 25;
    b.cc_rt = 3;
    s.remote[2].state = db::SlotState::Waiting;
    s.remote[3].state = db::SlotState::Lost;

    shot("ctl-bar-ready");
    {
        Input down;
        down.held = 1u << 1;
        ui.update(down, 0.016);
        frames(2);
    }
    shot("ctl-remote1-nunchuk-motionplus");
    {
        Input down;
        down.held = 1u << 1;
        ui.update(down, 0.016);
        frames(2);
    }
    shot("ctl-remote2-classic");
    for (int k = 0; k < 2; ++k)
    {
        Input down;
        down.held = 1u << 1;
        ui.update(down, 0.016);
        frames(2);
    }
    shot("ctl-remote4-lost");
    {
        db::Remote &sp = s.remote[0];
        sp.speaker_on = true;
        sp.speaker_volume = 1;
        sp.spk_sent = 60;
        sp.spk_refused = 2;
        sp.spk_rate = 74.6f;
        sp.spk_interval_min = 9.8f;
        sp.spk_interval_avg = 13.4f;
        sp.spk_interval_max = 31.0f;
        sp.spk_write_max_ms = 18;
        Input down;
        down.held = 1u << 1;
        ui.update(down, 0.016);
        frames(2);
    }
    shot("ctl-speaker");
    {
        Input up;
        up.held = 1u << 0;
        for (int k = 0; k < 6; ++k)
        {
            ui.update(up, 0.016);
            frames(2);
        }
    }
    shot("ctl-dualsense");
    {
        Input down;
        down.held = 1u << 1;
        for (int k = 0; k < 7; ++k)
        {
            ui.update(down, 0.016);
            frames(2);
        }
    }
    shot("ctl-setting");

    db::clear_notices();
    db::push_notice(1, true);
    db::push_notice(2, false);
    db::notices();
    std::this_thread::sleep_for(std::chrono::milliseconds(500)); /* faded in */
    frames(10);
    render("ctl-notices", [&] {
        ui.draw(12.0);
        porpoise::ui::draw_wiimote_notices(gfx);
    });
    ui.preview_controllers(0, true);
    shot("ctl-settings-section");
    porpoise::ui::apply_language(9, ""); /* German */
    ui.preview_controllers(0, true);
    shot("ctl-settings-section-de");
    ui.preview_controllers(2, false);
    frames(2);
    {
        Input down;
        down.held = 1u << 1;
        ui.update(down, 0.016);
        frames(2);
    }
    shot("ctl-remote1-de");
    /* Wii Remote Setup with real Wii Remotes: the first one has a Nunchuk. */
    ui.preview_real_remote_setup();
    shot("ctl-real-remote-setup-de");
    /* The prompts as a Wii Remote's buttons (the last controller pressed). */
    ui.preview_controllers(0, true);
    frames(40);
    gfx.set_wii_buttons(true);
    render("ctl-wii-prompts-de", [&] { ui.draw(12.0); });
    gfx.set_wii_sideways(true); /* no Nunchuk: the Remote held sideways */
    render("ctl-wii-prompts-sideways-de", [&] { ui.draw(12.0); });
    gfx.set_wii_sideways(false);
    gfx.set_wii_buttons(false);

    /* The Switch 2's GameCube controller: not plugged in, failed, working. */
    namespace gc = porpoise::ns2gc;
    porpoise::ui::apply_language(1, "");
    gc::Status c;
    gc::preview_status(&c);
    ui.preview_controllers(7, false);
    shot("ctl-gc-not-connected");
    c.link = gc::Link::Failed;
    c.node = "ugen1.4";
    c.product = "Nintendo GameCube Controller";
    c.failed_step = "its set-up (no command was answered)";
    c.commands_sent = 11;
    c.kernel_driver[0] = 1;
    shot("ctl-gc-failed");
    c = gc::Status{};
    c.link = gc::Link::Ready;
    c.node = "ugen1.4";
    c.product = "Nintendo GameCube Controller";
    c.serial = "HEJ70012345678";
    c.stick_calibration = c.trigger_calibration = true;
    c.buttons = gc::GcA | gc::GcZ | gc::GcLeft;
    c.raw_buttons[0] = 0x88;
    c.raw_buttons[2] = 0x08;
    c.lx = 0.7f;
    c.ly = 0.4f;
    c.rx = -0.3f;
    c.ry = -0.8f;
    c.raw_stick[0] = 3030, c.raw_stick[1] = 2600, c.raw_stick[2] = 1660, c.raw_stick[3] = 1010;
    c.lt = 0.35f;
    c.rt = 1.0f;
    c.raw_trigger[0] = 101, c.raw_trigger[1] = 232;
    c.report_rate = 250;
    c.reports = 12000;
    shot("ctl-gc-ready");
    porpoise::ui::apply_language(9, "");
    shot("ctl-gc-ready-de");
    ui.preview_controllers(0, true);
    shot("ctl-settings-section-gc-de");
    /* A USB Bluetooth adapter's test, done, a Switch 2 controller found. */
    namespace bt = porpoise::bt;
    bt::Status bs;
    bs.phase = bt::Phase::Scanning;
    bs.link = bt::Link::Ready;
    const std::uint8_t peer[6] = {0x3c, 0xa9, 0xab, 0x5d, 0xd7, 0xf6};
    std::memcpy(bs.peer, peer, 6);
    bs.att_mtu = 185, bs.interval_x125 = 6, bs.commands_ok = bs.commands_sent = 18, bs.notifications = 4210;
    bs.node = "ugen1.5";
    bs.vid = 0x37ad;
    bs.pid = 0x0600;
    bs.product = "TP-Link Bluetooth USB Adapter";
    bs.have_version = true;
    bs.hci_ver = 0x0a, bs.hci_rev = 0x000b, bs.lmp_ver = 0x0a, bs.lmp_subver = 0x8761, bs.manufacturer = 93;
    bs.chip = "Realtek RTL8761BU";
    bs.rom_version = 1;
    bs.firmware = "rtl8761bu_fw.bin uploaded (40214 bytes), now 000bdfc6";
    bs.firmware_ok = true;
    const std::uint8_t addr[6] = {0x58, 0x04, 0x4f, 0xd7, 0xdc, 0x91};
    std::memcpy(bs.bd_addr, addr, 6);
    bs.have_addr = bs.le = true;
    bt::Found f1;
    const std::uint8_t a1[6] = {0x98, 0xe2, 0x55, 0x12, 0x34, 0x56};
    std::memcpy(f1.addr, a1, 6);
    f1.rssi = -48, f1.nintendo = true, f1.company = 0x0553, f1.seen = 40;
    bt::Found f2;
    const std::uint8_t a2[6] = {0x4c, 0x11, 0xae, 0x01, 0x02, 0x03};
    std::memcpy(f2.addr, a2, 6);
    f2.rssi = -77, f2.name = "Living room TV", f2.seen = 12;
    bs.found = {f1, f2};
    bt::preview_status(&bs);
    ui.preview_controllers(8, false);
    shot("ctl-bt-done-de");
    bt::preview_status(nullptr);
    gc::preview_status(nullptr);
    db::preview_status(nullptr);
    return 0;
}
