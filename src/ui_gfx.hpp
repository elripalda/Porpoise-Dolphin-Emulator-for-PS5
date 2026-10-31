/* Porpoise UI - the renderer: quads, covers in perspective, SDF text, shader shapes.
 * Copyright (C) 2026 Ruben (Project Porpoise)
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Everything is laid out in a 1920x1080 design space and scaled to the
 * screen. The renderer is device-agnostic: it can live on Porpoise's own
 * Vulkan device (the launcher) and be rebuilt on the core's device (the
 * launch screen, overlays) - init() and shutdown() are the whole lifecycle. */
#pragma once

#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#ifndef VK_NO_PROTOTYPES
#define VK_NO_PROTOTYPES
#endif
#include <vulkan/vulkan.h>

namespace porpoise::ui
{
struct Color
{
    float r = 1, g = 1, b = 1, a = 1;
};
Color rgba(std::uint32_t hex, float alpha = 1.0f);

struct Texture
{
    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    VkDescriptorSet set = VK_NULL_HANDLE;
    int width = 0, height = 0;
    /* A streamed texture (Gfx::stream_texture): a staging buffer per frame
     * slot, and the slot whose pixels are waiting to be copied (-1 none). */
    VkBuffer staging[2] = {VK_NULL_HANDLE, VK_NULL_HANDLE};
    VkDeviceMemory staging_memory[2] = {VK_NULL_HANDLE, VK_NULL_HANDLE};
    void *staging_mapped[2] = {nullptr, nullptr};
    int pending = -1;
    /* Made by texture_file_async: its first copy goes in from staging[0]
     * (from no layout), and the staging buffer is let go after it. */
    bool fresh = false;
};

enum class Font
{
    Regular,
    SemiBold,
    Bold,
    ExtraBold,
};

enum class Align
{
    Left,
    Center,
    Right,
};

enum class Glyph
{
    Cross = 0,
    Circle = 1,
    Square = 2,
    Triangle = 3,
    Arrow = 4,   /* filled triangle, rotated */
    Pointer = 5, /* outlined triangle pointing up */
    DPad = 6,
    Star = 7,        /* filled: a favourite */
    StarOutline = 8, /* outlined */
    /* The DualSense's other controls, drawn from the button icons. */
    L1 = 10,
    R1 = 11,
    L2 = 12,
    R2 = 13,
    Options = 14,
    TouchPad = 15,
    Create = 16,
    L3 = 17,
    R3 = 18,
    LStick = 19,
    RStick = 20,
};

/* The button icons in assets/ui/buttons.png (Zacksly's "PS5 Button Icons and
 * Controls", CC BY 3.0, drawn by tools/make-controller-art.py), in its order. */
enum class Icon
{
    Cross, Circle, Square, Triangle,
    CrossSolid, CircleSolid, SquareSolid, TriangleSolid,
    L1, R1, L2, R2,
    Options, Create, TouchPad, DPad,
    DPadUp, DPadDown, DPadLeft, DPadRight,
    LStick, RStick, L3, R3,
    LStickAll, RStickAll,
    Count
};

struct GfxInit
{
    VkInstance instance = VK_NULL_HANDLE;
    VkPhysicalDevice gpu = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkQueue queue = VK_NULL_HANDLE;
    std::uint32_t queue_family = 0;
    VkRenderPass render_pass = VK_NULL_HANDLE;
    unsigned slots = 2;
    PFN_vkGetInstanceProcAddr get_instance_proc = nullptr;
    PFN_vkGetDeviceProcAddr get_device_proc = nullptr;
    std::mutex *queue_mutex = nullptr; /* held around this renderer's own submits */
    std::string asset_dir;             /* fonts/ and brand/ live here */
    std::string cache_dir;             /* the baked text atlases are kept here, if set */
    /* Japanese, Chinese or Korean: the font in fonts/ the menus fall back to
     * beyond Nunito, and characters to take from the other such fonts when it
     * lacks them (the languages' own names). */
    std::string cjk_font = "NotoSansJP-Porpoise.ttf";
    std::string cjk_also;
};

/* How a piece of glass looks (Gfx::glass). */
struct Glass
{
    Color tint;          /* body */
    Color rim;           /* rim and glow */
    float radius = 14;
    float rim_w = 2;
    float glow = 0;
    float phase = 0;     /* where the room's reflections fall on it, e.g. its x / 1920 */
    float light = 1;     /* brightness of a side slice */
    int face = 0;        /* 0 front, 1 side slice, 2 gloss over a picture */
    float fade = 1;      /* the whole piece's opacity */
};

/* How the theme looks to the renderer, set by the menus each frame. */
struct Look
{
    int background = 0;    /* the shader's room (BG_* in shaders/ui.frag) */
    int panel_style = 0;   /* 0 the classic panels, 1 liquid glass */
    int colour_filter = 0; /* 0 off, 1 red-weak, 2 green-weak, 3 blue-weak, 4 greyscale */
    bool still = false;    /* the background holds still */
    Color light0{0.05f, 0.62f, 1.0f, 1.0f}; /* the room's two lights */
    Color light1{0.48f, 0.36f, 1.0f, 1.0f};
    float effect = 1;      /* the theme's effect strength (a tube's bend, say) */
    /* Colours given to the drawing calls (pictures are left alone): the blue
     * family moved to another hue, saturation, dark fills, one-colour themes. */
    float hue = -1;        /* the blues' new hue 0..1; -1 keeps them */
    float hue_spread = 1;  /* how far apart the blues stay */
    float saturation = 1;
    float dark = 1;        /* lightness of dark fills */
    float fill_alpha = 1;  /* opacity of dark fills */
    bool mono = false;     /* everything but warnings in mono's hue */
    Color mono_color{};
    bool high_contrast = false;
    bool bold_focus = false; /* what is chosen drawn with a thicker, brighter edge */
    bool flat = false;     /* no glows or sheen: plain, crisp panels */
    float calm = 0;        /* 0..1: quieter room, glass and covers (fainter grid, no beams, less spectrum) */
    float content_scale = 1; /* everything but the room shrunk about the middle (a TV's frame round it) */
};

/* A screen-space corner after projection: what the vertex shader receives. */
struct Corner
{
    float x, y, w; /* design-space position and the perspective w */
};

class Gfx
{
public:
    bool init(const GfxInit &init);
    void shutdown();
    bool ready() const { return device_ != VK_NULL_HANDLE; }
    /* After the menus' language changes: GfxInit's cjk_font and cjk_also
     * again; the CJK atlas is rebuilt (or read from the cache) if they differ. */
    void set_cjk_font(const std::string &file, const std::string &also);
    /* The theme, for this frame and the next (it stays until changed). */
    void set_look(const Look &look) { look_ = look; }
    const Look &look() const { return look_; }
    /* A full-screen overlay: 1 a tube's scanlines and edges, 2 a terminal's,
     * 3 a worn tape's. */
    void fx(int which, float strength = 1);
    bool cjk_busy() const { return cjk_job_ != nullptr; } /* a new CJK atlas is on its way */
    /* The theme's own letters: "" Nunito, "mono" JetBrains Mono (Terminal),
     * "vt" VT323 (Broadcast). Made on a worker the first time (then cached);
     * Nunito until they're ready, and for anything they lack. */
    void set_theme_fonts(const std::string &set);
    bool theme_fonts_busy() const { return theme_job_ != nullptr; }

    /* Textures. RGBA8, straight alpha. */
    Texture *texture_rgba(const std::uint8_t *pixels, int width, int height);
    /* PNG / JPG, halved until its longer side fits max_side. */
    Texture *texture_file(const std::string &path, int max_side = 1024);
    /* The same, read and decoded on a worker thread so a screen of new covers
     * doesn't hold a frame up: nullptr with *pending true while it decodes,
     * then the texture (the caller's, as from texture_file). A couple become
     * textures each frame; their pixels go in with the frame's own commands. */
    Texture *texture_file_async(const std::string &path, bool *pending, int max_side = 1024);
    void set_async_loads(bool on) { async_loads_ = on; } /* off: texture_file (the preview tool) */
    void free_texture(Texture *texture);
    /* A texture whose pixels change often (a banner playing): stream_update()
     * hands it this frame's pixels, and record_uploads() - called before the
     * frame's render pass - copies them in, without waiting on the GPU. */
    Texture *stream_texture(int width, int height);
    void stream_update(Texture *texture, const std::uint8_t *pixels);
    void record_uploads(VkCommandBuffer cmd);
    Texture *brand_mask() const { return brand_mask_; }
    const std::string &asset_dir() const { return init_.asset_dir; }

    /* A frame: begin, draw, then record() into the render pass that is open
     * on cmd. Target size is the framebuffer in pixels. */
    void begin(unsigned slot, float target_w, float target_h, float time, float dim, bool reduced_motion);
    /* Into the present's own slot when given (3.1: frame generation can
     * present twice for one frame of the menus, each its own buffer). */
    void record(VkCommandBuffer cmd, int slot = -1);

    /* Everything drawn after this is moved by (dx, dy) design pixels and faded:
     * how a screen slides in. begin() resets it. */
    void set_layer(float dx = 0, float dy = 0, float fade = 1)
    {
        layer_dx_ = dx;
        layer_dy_ = dy;
        layer_fade_ = fade;
    }

    /* The light look (Revolution): colours given to the drawing calls below
     * are turned light for dark and dark for light, keeping their hue, so
     * the screens drawn for the dark look read on white. Pictures are left
     * alone. begin() turns it off; screens drawn for the light look keep it
     * off. */
    void set_tone(bool light) { tone_ = light; }
    /* On top of set_layer, for the whole frame: the menus' entrance. Not the
     * background. begin() resets it. */
    void set_intro(float fade, float dy)
    {
        intro_fade_ = fade;
        intro_dy_ = dy;
    }
    /* Pictures asked for in the last frames still being read or copied in. */
    bool loading() const;
    bool toned() const { return tone_; }
    /* Panels drawn plain (no liquid glass) while set: a screen, a sticker. */
    void set_solid(bool solid) { solid_ = solid; }
    Color tone(Color c) const;

    /* Where a picture was drawn: the largest place it took in the last
     * whole frame (the launch's cover glides from there). watch() names the
     * picture for the frame being drawn. */
    void watch(Texture *t) { watch_tex_ = t; }
    bool watched(float rect[4]) const
    {
        if (watch_last_[2] <= 0)
            return false;
        for (int i = 0; i < 4; ++i)
            rect[i] = watch_last_[i];
        return true;
    }

    /* Drawing, in design-space pixels (1920x1080). alpha < 1: over what is
     * drawn already (a screen fading in). */
    void background(float alpha = 1.0f);
    void panel(float x, float y, float w, float h, Color fill, float bottom_mul, float radius,
               Color border = {}, float border_w = 0, float glow = 0, float sheen = 0);
    void image(Texture *t, float x, float y, float w, float h, Color tint = {}, float radius = 0);
    void blob(float cx, float cy, float w, float h, Color c);
    void glyph(Glyph g, float cx, float cy, float size, Color c, float rotation = 0);
    /* A button icon in a size x size cell (the icons carry their own margin:
     * a face button's ring is about two thirds of the cell). */
    void icon(Icon i, float cx, float cy, float size, Color c);
    bool has_icons() const { return icons_ != nullptr; }
    /* The prompts as a Wii Remote's buttons (A, B, 1, +, -, and the Nunchuk's
     * C and Z) while one works the menus; the rest stay the DualSense's. */
    void set_wii_buttons(bool on) { wii_buttons_ = on; }
    bool wii_buttons() const { return wii_buttons_; }
    void set_wii_sideways(bool on) { wii_sideways_ = on; } /* held sideways: 1, 2, A, B and B+1, B+2, B+- */
    bool wii_sideways() const { return wii_sideways_; }
    /* Part of a texture: uv is u0 v0 u1 v1. */
    void image_part(Texture *t, float x, float y, float w, float h, const float uv[4], Color tint = {},
                    float radius = 0);
    /* A textured quad from four projected corners (top-left, top-right,
     * bottom-right, bottom-left); shape_w/h is the quad's own size before
     * projection, for rounded corners. reflection fades it out downward. */
    void quad3d(Texture *t, const Corner c[4], float shape_w, float shape_h, Color tint, float radius,
                bool reflection, bool flip_v, const float *uv_rect = nullptr /* u0 v0 u1 v1 */);
    void panel3d(const Corner c[4], float shape_w, float shape_h, float margin, Color fill, float bottom_mul,
                 float radius, Color border, float border_w, float glow, float sheen);
    /* Glass from four projected corners; margin is room around the shape for the glow. */
    void glass(const Corner c[4], float shape_w, float shape_h, float margin, const Glass &g);

    /* Text laid on any surface: map takes a point of the line (x along it from
     * its start as aligned, y down from the middle of its capitals) to the
     * screen - a cube's face in perspective, a turned label. */
    void text_mapped(Font f, float size, const std::string &s, Color c, Align a,
                     const std::function<Corner(float x, float y)> &map, float weight = 0);

    /* Text. y is the top of the line box; returns the advance width. */
    float text(Font f, float size, float x, float y, Color c, Align a, const std::string &s,
               float spacing = 0, float weight = 0);
    float measure(Font f, float size, const std::string &s, float spacing = 0) const;
    /* Text whose capital letters are centred on cy: what labels inside
     * buttons, tabs and rows need to look centred. */
    float text_mid(Font f, float size, float x, float cy, Color c, Align a, const std::string &s,
                   float spacing = 0, float weight = 0);
    float line_height(Font f, float size) const;

    float design_w() const { return 1920.0f; }
    float design_h() const { return 1080.0f; }

private:
    struct Vertex
    {
        float pos[4];
        float uv[2];
        float local[2];
        float color[4];
        float p0[4];
        float p1[4];
        float p2[4];
        float bcolor[4];
    };
    struct Batch
    {
        VkDescriptorSet set;
        std::uint32_t first, count;
    };
    struct GlyphInfo
    {
        bool present = false;
        float u0, v0, u1, v1;   /* atlas UVs */
        float xoff, yoff, w, h; /* at base size, pixels */
        float advance;
        bool cjk = false;   /* in the CJK font's atlas */
        bool theme = false; /* in the theme's own fonts' atlas */
    };
    struct FontData
    {
        std::vector<unsigned char> ttf;
        float ascent = 0, descent = 0, line_gap = 0; /* at base size */
        float cap = 0;                               /* capital height at base size */
        GlyphInfo glyphs[128];
        /* Beyond ASCII: accented letters for Spanish, French and Portuguese,
         * and typographic marks; sorted by code point. */
        std::vector<std::pair<std::uint32_t, GlyphInfo>> extra;
        void *info = nullptr; /* stbtt_fontinfo */
        float scale = 1;
    };

    bool load_functions();
    bool create_pipeline();
    struct Pen
    {
        int x = 1, y = 1, row_h = 0;
    };
    struct CjkJob;
    bool build_fonts();
    void first_cjk();
    bool make_cjk(const std::string &font, const std::string &also, FontData &cjk,
                  std::vector<std::uint8_t> &pixels) const;
    static void *cjk_work(void *job);
    void finish_cjk_job(bool adopt);
    bool load_font(const std::string &file, FontData &fd) const;
    static void unload_font(FontData &fd);
    void bake(FontData &fd, std::uint32_t cp, GlyphInfo &g, std::vector<std::uint8_t> &atlas, Pen &pen) const;
    bool load_atlas_cache(const std::string &path, FontData *const *fonts, int count,
                          std::vector<std::uint8_t> &atlas) const;
    bool save_atlas_cache(const std::string &path, const FontData *const *fonts, int count,
                          const std::vector<std::uint8_t> &atlas) const;
    const GlyphInfo *find(const FontData &f, std::uint32_t cp) const;
    void push(Texture *t, const Vertex v[4]);
    void corners_flat(float x, float y, float w, float h, float out[4][4]) const;
    void to_clip(const Corner &c, float out[4]) const;
    std::uint32_t memory_type(std::uint32_t bits, VkMemoryPropertyFlags flags) const;
    bool make_buffer(VkDeviceSize size, VkBufferUsageFlags usage, VkBuffer &buffer, VkDeviceMemory &memory,
                     void **mapped);
    Texture *upload(const std::uint8_t *pixels, int width, int height);
    Texture *upload_later(const std::uint8_t *pixels, int width, int height);
    void destroy_texture(Texture *t);

    GfxInit init_{};
    VkDevice device_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout set_layout_ = VK_NULL_HANDLE;
    VkPipelineLayout layout_ = VK_NULL_HANDLE;
    VkPipeline pipeline_ = VK_NULL_HANDLE;
    VkSampler sampler_ = VK_NULL_HANDLE;
    VkDescriptorPool pool_ = VK_NULL_HANDLE;
    VkCommandPool upload_pool_ = VK_NULL_HANDLE;
    std::vector<VkBuffer> vbufs_;
    std::vector<VkDeviceMemory> vmems_;
    std::vector<void *> vmaps_;
    std::size_t vcapacity_ = 0; /* vertices per slot */

    bool tone_ = false;
    bool solid_ = false; /* set_solid */
    Texture *white_ = nullptr;
    Texture *atlas_ = nullptr;
    Texture *icons_ = nullptr;
    bool wii_buttons_ = false, wii_sideways_ = false;
    Texture *brand_mask_ = nullptr;
    std::vector<Texture *> textures_;
    /* Freed, waiting for the GPU: the UI frame and the present (record())
     * it was freed at. Frame generation can present more than once a frame,
     * or not at all in one: it goes once both have come round. */
    struct Grave
    {
        Texture *texture;
        std::uint64_t frame, record;
    };
    std::vector<Grave> graveyard_;
    std::uint64_t records_ = 0;
    std::uint64_t frame_no_ = 0;
    bool async_loads_ = true;
    int uploads_left_ = 0; /* texture_file_async's this frame */
    FontData fonts_[4];
    std::vector<std::uint8_t> atlas_pixels_; /* kept across device changes */
    /* Japanese, Chinese or Korean (init_.cjk_font): one weight of Noto Sans,
     * subset to the characters the menus use, in an atlas of its own; any font
     * falls back to it. */
    FontData cjk_;
    Texture *cjk_atlas_ = nullptr;
    std::vector<std::uint8_t> cjk_pixels_;
    std::string cjk_font_, cjk_also_; /* what cjk_ and its atlas hold */
    CjkJob *cjk_job_ = nullptr;       /* the next one, being made */
    /* A theme's own fonts, in the same four weights, in an atlas of their own. */
    struct ThemeJob;
    FontData theme_fonts_[4];
    Texture *theme_atlas_ = nullptr;
    std::vector<std::uint8_t> theme_pixels_;
    std::string theme_set_;          /* what theme_fonts_ hold */
    ThemeJob *theme_job_ = nullptr;
    bool make_theme_fonts(const std::string &set, FontData *out, std::vector<std::uint8_t> &pixels) const;
    static void *theme_work(void *job);
    void finish_theme_job(bool adopt);
    const FontData &face(Font f) const
    {
        return theme_atlas_ && !theme_set_.empty() ? theme_fonts_[int(f)] : fonts_[int(f)];
    }
    bool fonts_built_ = false;

    unsigned slot_ = 0;
    float target_w_ = 1920, target_h_ = 1080;
    float time_ = 0, dim_ = 0;
    float layer_dx_ = 0, layer_dy_ = 0, layer_fade_ = 1;
    Texture *watch_tex_ = nullptr;
    float watch_cur_[4] = {0, 0, 0, 0}, watch_last_[4] = {0, 0, 0, 0}; /* x y w h */
    void note_watched(Texture *t, float x0, float y0, float x1, float y1)
    {
        if (!t || t != watch_tex_ || (x1 - x0) * (y1 - y0) <= watch_cur_[2] * watch_cur_[3])
            return;
        watch_cur_[0] = x0;
        watch_cur_[1] = y0;
        watch_cur_[2] = x1 - x0;
        watch_cur_[3] = y1 - y0;
    }
    float intro_fade_ = 1, intro_dy_ = 0;
    Look look_{};
    Color map_colour(Color c) const;
    bool reduced_motion_ = false;
    std::vector<Vertex> vertices_;
    std::vector<Batch> batches_;
    bool overflowed_ = false;

#define PORPOISE_UI_VK_FUNCS(X)                                                                  \
    X(vkCreateBuffer) X(vkDestroyBuffer) X(vkGetBufferMemoryRequirements) X(vkAllocateMemory)    \
    X(vkFreeMemory) X(vkBindBufferMemory) X(vkMapMemory) X(vkUnmapMemory) X(vkCreateImage)       \
    X(vkDestroyImage) X(vkGetImageMemoryRequirements) X(vkBindImageMemory) X(vkCreateImageView)  \
    X(vkDestroyImageView) X(vkCreateSampler) X(vkDestroySampler) X(vkCreateDescriptorSetLayout) \
    X(vkDestroyDescriptorSetLayout) X(vkCreateDescriptorPool) X(vkDestroyDescriptorPool)        \
    X(vkAllocateDescriptorSets) X(vkFreeDescriptorSets) X(vkUpdateDescriptorSets)               \
    X(vkCreatePipelineLayout) X(vkDestroyPipelineLayout) X(vkCreateShaderModule)                \
    X(vkDestroyShaderModule) X(vkCreateGraphicsPipelines) X(vkDestroyPipeline)                  \
    X(vkCreateCommandPool) X(vkDestroyCommandPool) X(vkAllocateCommandBuffers)                  \
    X(vkFreeCommandBuffers) X(vkBeginCommandBuffer) X(vkEndCommandBuffer)                        \
    X(vkCmdPipelineBarrier) X(vkCmdCopyBufferToImage) X(vkQueueSubmit) X(vkQueueWaitIdle)       \
    X(vkCmdBindPipeline) X(vkCmdBindVertexBuffers) X(vkCmdBindDescriptorSets)                   \
    X(vkCmdPushConstants) X(vkCmdSetViewport) X(vkCmdSetScissor) X(vkCmdDraw)                    \
    X(vkDeviceWaitIdle)
#define PORPOISE_UI_DECLARE(name) PFN_##name name##_ = nullptr;
    PORPOISE_UI_VK_FUNCS(PORPOISE_UI_DECLARE)
#undef PORPOISE_UI_DECLARE
    PFN_vkGetPhysicalDeviceMemoryProperties vkGetPhysicalDeviceMemoryProperties_ = nullptr;
};
} // namespace porpoise::ui
