/* PC tests of src/porpoise_bt.cpp's pure parts: Realtek's patch files (the
 * ones in assets/bluetooth) and Bluetooth LE advertising data. Not a test of
 * an adapter or of the PS5.
 * Copyright (C) 2026 Ruben (Project Porpoise)
 * SPDX-License-Identifier: GPL-3.0-or-later */
#include "porpoise_bt.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace b = porpoise::bt;
namespace
{
int g_failures = 0;
#define CHECK(c)                                                                                                       \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(c))                                                                                                      \
        {                                                                                                              \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);                                                   \
            ++g_failures;                                                                                              \
        }                                                                                                              \
    } while (0)

std::vector<std::uint8_t> load(const std::string &path)
{
    std::ifstream f(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
}

void test_patch(const std::string &assets, const char *fw_name, const char *cfg_name)
{
    const auto fw = load(assets + "/" + fw_name), cfg = load(assets + "/" + cfg_name);
    CHECK(fw.size() > 1000 && !cfg.empty());
    const int n = fw[12] | (fw[13] << 8);
    std::printf("%s: %zu bytes, %d patches, chip ids:", fw_name, fw.size(), n);
    int found = 0;
    for (int i = 0; i < n; ++i)
    {
        const int id = fw[14 + 2 * i] | (fw[15 + 2 * i] << 8);
        std::printf(" %d", id);
        std::string why;
        const auto p = b::realtek_patch(fw, cfg, id - 1, why);
        CHECK(!p.empty() && why.empty());
        if (!p.empty())
        {
            ++found;
            /* it ends in the file's version, then the config */
            CHECK(std::memcmp(p.data() + p.size() - cfg.size(), cfg.data(), cfg.size()) == 0);
            CHECK(std::memcmp(p.data() + p.size() - cfg.size() - 4, fw.data() + 8, 4) == 0);
        }
    }
    std::printf("\n");
    CHECK(found == n && n > 0);
    std::string why;
    CHECK(b::realtek_patch(fw, cfg, 200, why).empty() && !why.empty());
    auto bad = fw;
    bad[0] = 'X';
    CHECK(b::realtek_patch(bad, cfg, 0, why).empty());
}

void test_advertising()
{
    /* flags, the full name, Nintendo's manufacturer data */
    const std::uint8_t ad[] = {2, 0x01, 0x06, 5, 0x09, 'N', 'S', 'O', '2', 6, 0xff, 0x53, 0x05, 0x01, 0x02, 0x03};
    b::Found f;
    b::parse_advertising(ad, sizeof ad, f);
    CHECK(f.name == "NSO2");
    CHECK(f.company == 0x0553 && f.nintendo);
    CHECK(f.maker.size() == 3 && f.maker[2] == 0x03);
    b::Found g;
    const std::uint8_t broken[] = {9, 0x09, 'x'};
    b::parse_advertising(broken, sizeof broken, g);
    CHECK(g.name.empty() && g.company == -1);
    const std::uint8_t a[6] = {0x98, 0xb6, 0xe9, 0x01, 0x02, 0xff};
    CHECK(b::address_text(a) == "98:B6:E9:01:02:FF");
    CHECK(b::chip_name(93, 0x8761, 0x000b) == "Realtek RTL8761BU");
    /* the user's controller's advertisement on a PS5 (2026-11-01) */
    const std::uint8_t gc[] = {2,    0x01, 0x06, 27,   0xff, 0x53, 0x05, 0x01, 0x00, 0x03, 0x7e, 0x05, 0x73,
                               0x20, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0f, 0x00, 0x00,
                               0x00, 0x00, 0x00, 0x00, 0x00};
    b::Found h;
    b::parse_advertising(gc, sizeof gc, h);
    CHECK(b::is_gamecube_controller(h));
    CHECK(!b::is_gamecube_controller(f));
}
} // namespace

int main(int argc, char **argv)
{
    const std::string assets = argc > 1 ? argv[1] : "assets/bluetooth";
    test_patch(assets, "rtl8761bu_fw.bin", "rtl8761bu_config.bin");
    test_patch(assets, "rtl8761cu_fw.bin", "rtl8761cu_config.bin");
    test_advertising();
    std::printf("%s (%d failed)\n", g_failures ? "FAILED" : "all passed", g_failures);
    return g_failures ? 1 : 0;
}
