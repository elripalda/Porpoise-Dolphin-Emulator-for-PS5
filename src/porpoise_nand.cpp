/* Porpoise - a BootMii NAND backup into Dolphin's Wii folder (see the header).
 * The same steps as Dolphin's DiscIO::NANDImporter (GPL-2.0-or-later), read
 * from the file a cluster at a time instead of all 512 MiB at once: the
 * newest of the 16 SFFS superblocks, then every file in its table, each
 * cluster decrypted with the NAND key (AES-128-CBC, a zero IV per cluster),
 * names escaped as Dolphin's host file system expects, and IOS13's
 * certificates written out as Dolphin does.
 * Copyright (C) 2026 Ruben (Project Porpoise)
 * SPDX-License-Identifier: GPL-3.0-or-later */
#include "porpoise_nand.hpp"

#include <algorithm>
#include <bitset>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <sys/stat.h>
#include <vector>

#include "porpoise_disc.hpp"
#include "trace.hpp"

namespace porpoise::nand
{
namespace
{
using u8 = std::uint8_t;
using u16 = std::uint16_t;
using u32 = std::uint32_t;
using u64 = std::uint64_t;

constexpr u64 kPage = 0x800, kSpare = 0x40, kPages = 0x40000;
constexpr u64 kBinSize = (kPage + kSpare) * kPages; /* 0x21000000 */
constexpr u64 kKeysSize = 0x400;
constexpr u64 kCluster = 0x4000;                    /* 8 pages */
constexpr u64 kSuperStart = 0x1FC00000;             /* in the NAND's own bytes */
constexpr u64 kSuperSize = 0x40000;
constexpr int kFstCount = 0x17FF;
constexpr u16 kChainEnd = 0xFFFB;
constexpr u64 kKeyOffset = 0x158;                   /* the NAND key in keys.bin */

u16 be16(const u8 *p) { return u16(p[0] << 8 | p[1]); }
u32 be32(const u8 *p) { return u32(p[0]) << 24 | u32(p[1]) << 16 | u32(p[2]) << 8 | p[3]; }

struct Entry
{
    std::string name;
    u8 mode = 0;
    u16 sub = 0, sib = 0;
    u32 size = 0;
};

class Nand
{
public:
    std::FILE *f = nullptr;
    u8 key[16] = {};
    std::vector<u16> fat;
    std::vector<Entry> fst;
    ~Nand()
    {
        if (f)
            std::fclose(f);
    }
    /* The NAND's own bytes of a cluster (its pages without their spare). */
    bool raw_cluster(u64 cluster, u8 *out)
    {
        u8 pages[8 * (kPage + kSpare)];
        if (fseeko(f, off_t(cluster * 8 * (kPage + kSpare)), SEEK_SET) != 0 ||
            std::fread(pages, 1, sizeof pages, f) != sizeof pages)
            return false;
        for (int p = 0; p < 8; ++p)
            std::memcpy(out + p * kPage, pages + p * (kPage + kSpare), kPage);
        return true;
    }
    bool cluster(u64 c, u8 *out)
    {
        if (!raw_cluster(c, out))
            return false;
        u8 iv[16] = {};
        porpoise::disc::aes_cbc_decrypt(key, iv, out, kCluster);
        return true;
    }
    /* The newest superblock: its FAT and file table. */
    bool superblock()
    {
        std::vector<u8> best, sb(kSuperSize);
        u32 best_version = 0;
        for (int i = 0; i < 16; ++i)
        {
            const u64 first = (kSuperStart + u64(i) * kSuperSize) / kCluster;
            bool ok = true;
            for (u64 c = 0; c < kSuperSize / kCluster && ok; ++c)
                ok = raw_cluster(first + c, sb.data() + c * kCluster);
            if (!ok || std::memcmp(sb.data(), "SFFS", 4) != 0)
                continue;
            const u32 version = be32(sb.data() + 4);
            if (best.empty() || version > best_version)
            {
                best = sb;
                best_version = version;
            }
        }
        if (best.empty())
            return false;
        fat.resize(0x8000);
        for (int i = 0; i < 0x8000; ++i)
            fat[std::size_t(i)] = be16(best.data() + 12 + i * 2);
        fst.resize(kFstCount);
        const u8 *table = best.data() + 12 + 0x8000 * 2;
        for (int i = 0; i < kFstCount; ++i)
        {
            const u8 *e = table + i * 0x20;
            Entry &x = fst[std::size_t(i)];
            x.name.assign(reinterpret_cast<const char *>(e), strnlen(reinterpret_cast<const char *>(e), 12));
            x.mode = e[12];
            x.sub = be16(e + 14);
            x.sib = be16(e + 16);
            x.size = be32(e + 18);
        }
        char line[96];
        std::snprintf(line, sizeof line, "nand: superblock version %#x", best_version);
        ps5::debug::mark(line);
        return true;
    }
};

/* Dolphin's Common::EscapeFileName: its host file system finds the files by
 * these names. */
std::string escape(const std::string &name)
{
    if (!name.empty() && std::all_of(name.begin(), name.end(), [](char c) { return c == '.'; }))
    {
        std::string out;
        for (std::size_t i = 0; i < name.size(); ++i)
            out += "__2e__";
        return out;
    }
    std::string doubled;
    for (std::size_t i = 0; i < name.size();)
    {
        if (name.compare(i, 2, "__") == 0)
        {
            doubled += "__5f____5f__";
            i += 2;
        }
        else
            doubled += name[i++];
    }
    static const char kIllegal[] = {'"', '*', '/', ':', '<', '>', '?', '\\', '|', '\x7f'};
    std::string out;
    for (char c : doubled)
    {
        const bool illegal = static_cast<unsigned char>(c) <= 0x1F || std::find(std::begin(kIllegal), std::end(kIllegal), c) != std::end(kIllegal);
        if (illegal)
        {
            char hex[8];
            std::snprintf(hex, sizeof hex, "__%02x__", static_cast<unsigned char>(c));
            out += hex;
        }
        else
            out += c;
    }
    return out;
}

void make_dirs(const std::string &path)
{
    for (std::size_t i = 1; i < path.size(); ++i)
        if (path[i] == '/')
            mkdir(path.substr(0, i).c_str(), 0777);
    mkdir(path.c_str(), 0777);
}

bool exists(const std::string &path)
{
    struct stat st;
    return ::stat(path.c_str(), &st) == 0;
}

class Importer
{
public:
    Nand nand;
    std::string root, kept_root;
    Progress *p = nullptr;
    std::bitset<kFstCount> seen;

    void fail(const std::string &why)
    {
        std::lock_guard<std::mutex> lock(p->m);
        p->error = why;
        p->state.store(2);
    }

    bool file(const Entry &e, const std::string &path)
    {
        std::vector<u8> data;
        std::vector<u8> block(kCluster);
        u16 c = e.sub;
        u64 left = e.size;
        if (left > u64(0x8000) * kCluster)
            left = 0; /* more than the whole NAND: a damaged entry */
        data.reserve(left);
        while (left > 0)
        {
            if (c >= nand.fat.size() || !nand.cluster(c, block.data()))
            {
                /* A broken chain: an empty file, as Dolphin writes, and on. */
                ps5::debug::mark(("nand: damaged, written empty: " + path).c_str());
                data.clear();
                break;
            }
            const u64 n = std::min(left, kCluster);
            data.insert(data.end(), block.begin(), block.begin() + std::ptrdiff_t(n));
            left -= n;
            c = nand.fat[c];
        }
        const std::string dest = root + path;
        if (exists(dest))
        {
            /* Porpoise's own (a game's save made here): kept, not lost. */
            /* (kept_root is this import's own dated folder: nothing kept
             * by an earlier one is ever replaced.) */
            std::string aside = kept_root + path;
            for (int n = 2; exists(aside) && n < 100; ++n)
                aside = kept_root + path + "." + std::to_string(n);
            make_dirs(aside.substr(0, aside.rfind('/')));
            if (exists(aside) || std::rename(dest.c_str(), aside.c_str()) != 0)
                return false; /* Never replace an original that could not be kept. */
            ++p->kept;
        }
        std::FILE *out = std::fopen(dest.c_str(), "wb");
        if (!out)
            return false;
        const bool ok = data.empty() || std::fwrite(data.data(), 1, data.size(), out) == data.size();
        return std::fclose(out) == 0 && ok;
    }

    /* An entry and its siblings; a folder's own entries inside it. */
    bool walk(u16 n, const std::string &parent)
    {
        while (n != 0xFFFF)
        {
            if (n >= kFstCount || seen[n])
                return false; /* a damaged table */
            seen[n] = true;
            const Entry &e = nand.fst[n];
            const std::string path = n == 0 ? parent : parent + "/" + escape(e.name);
            if ((e.mode & 3) == 1)
            {
                if (!file(e, path))
                {
                    ps5::debug::mark(("nand: couldn't write " + path).c_str());
                    fail("A file couldn't be written: is the drive full?");
                    return false;
                }
                ++p->files;
                p->done.fetch_add(1);
            }
            else if ((e.mode & 3) == 2)
            {
                make_dirs(root + path);
                if (!walk(e.sub, path))
                    return false;
            }
            n = e.sib;
        }
        return true;
    }

    /* IOS13's certificates, as Dolphin writes them for the Wii's secure
     * connections (clientca.pem, clientcakey.pem, rootca.pem). */
    bool certificates()
    {
        const std::string dir = root + "/title/00000001/0000000d/content/";
        std::vector<u8> tmd;
        auto read_all = [](const std::string &path, std::vector<u8> &out) {
            out.clear();
            std::FILE *f = std::fopen(path.c_str(), "rb");
            if (!f)
                return false;
            u8 buf[65536];
            std::size_t n;
            while ((n = std::fread(buf, 1, sizeof buf, f)) > 0)
                out.insert(out.end(), buf, buf + n);
            std::fclose(f);
            return !out.empty();
        };
        if (!read_all(dir + "title.tmd", tmd) || tmd.size() < 0x1E4 + 36)
            return false;
        /* The TMD's boot content: its index at 0x1E0, the contents from 0x1E4. */
        const u16 boot = be16(tmd.data() + 0x1E0);
        const u16 count = be16(tmd.data() + 0x1DE);
        /* As Dolphin's TMDReader::GetContent: the boot index is a place in
         * the list. */
        if (boot >= count || 0x1E4 + std::size_t(boot + 1) * 36 > tmd.size())
            return false;
        const u32 id = be32(tmd.data() + 0x1E4 + std::size_t(boot) * 36);
        char name[16];
        std::snprintf(name, sizeof name, "%08x.app", id);
        std::vector<u8> app;
        if (!read_all(dir + name, app))
            return false;
        struct Pem
        {
            const char *file;
            u8 bytes[4];
        };
        static const Pem kPems[] = {{"/clientca.pem", {0x30, 0x82, 0x03, 0xE9}},
                                    {"/clientcakey.pem", {0x30, 0x82, 0x02, 0x5D}},
                                    {"/rootca.pem", {0x30, 0x82, 0x03, 0x7D}}};
        for (const Pem &pem : kPems)
        {
            const auto at = std::search(app.begin(), app.end(), pem.bytes, pem.bytes + 4);
            if (at == app.end())
                return false;
            const std::size_t offset = std::size_t(at - app.begin());
            if (offset < 2)
                return false;
            const u16 size = be16(app.data() + offset - 2);
            if (size > app.size() - offset)
                return false;
            std::FILE *f = std::fopen((root + pem.file).c_str(), "wb");
            if (!f)
                return false;
            const bool ok = std::fwrite(app.data() + offset, 1, size, f) == size;
            if (std::fclose(f) != 0 || !ok)
                return false;
        }
        return true;
    }
};
} // namespace

bool find_backup(const std::string &data_dir, std::string &bin, std::string &keys)
{
    bin.clear();
    keys.clear();
    std::vector<std::string> dirs = {data_dir + "/nand", "/data/porpoise/nand"};
    for (int i = 0; i < 8; ++i)
        dirs.push_back("/mnt/usb" + std::to_string(i));
    for (const std::string &d : dirs)
    {
        struct stat st;
        if (::stat((d + "/nand.bin").c_str(), &st) == 0 &&
            (u64(st.st_size) == kBinSize || u64(st.st_size) == kBinSize + kKeysSize))
        {
            bin = d + "/nand.bin";
            if (::stat((d + "/keys.bin").c_str(), &st) == 0 && u64(st.st_size) >= kKeysSize)
                keys = d + "/keys.bin";
            return true;
        }
    }
    return false;
}

constexpr const char *kMarker = "/porpoise-nand-imported.txt";

bool imported(const std::string &wii_root)
{
    return exists(wii_root + kMarker);
}

void import(const std::string &bin, const std::string &keys, const std::string &wii_root,
            const std::string &kept_root, Progress &p)
{
    Importer im;
    im.root = wii_root;
    im.kept_root = kept_root;
    im.p = &p;
    ps5::debug::mark(("nand: importing " + bin).c_str());
    im.nand.f = std::fopen(bin.c_str(), "rb");
    if (!im.nand.f)
    {
        im.fail("The backup couldn't be opened.");
        return;
    }
    fseeko(im.nand.f, 0, SEEK_END);
    const u64 size = u64(ftello(im.nand.f));
    if (size != kBinSize && size != kBinSize + kKeysSize)
    {
        im.fail("This file doesn't look like a BootMii NAND backup.");
        return;
    }
    /* The console's keys: at the end of a newer BootMii backup, else keys.bin. */
    u8 key_file[kKeysSize] = {};
    bool have_keys = false;
    if (size == kBinSize + kKeysSize)
        have_keys = fseeko(im.nand.f, off_t(kBinSize), SEEK_SET) == 0 &&
                    std::fread(key_file, 1, kKeysSize, im.nand.f) == kKeysSize;
    else if (!keys.empty())
        if (std::FILE *k = std::fopen(keys.c_str(), "rb"))
        {
            have_keys = std::fread(key_file, 1, kKeysSize, k) == kKeysSize;
            std::fclose(k);
        }
    if (!have_keys)
    {
        im.fail("This backup has no keys of its own: put the keys.bin BootMii made beside nand.bin.");
        return;
    }
    std::memcpy(im.nand.key, key_file + kKeyOffset, 16);
    if (!im.nand.superblock())
    {
        im.fail("No Wii file system was found in this backup (or the keys aren't this Wii's).");
        return;
    }
    p.total.store(int(std::count(im.nand.fat.begin(), im.nand.fat.end(), kChainEnd)));
    make_dirs(wii_root);
    /* Dolphin keeps the console's keys with its NAND. */
    if (std::FILE *k = std::fopen((wii_root + "/keys.bin").c_str(), "wb"))
    {
        std::fwrite(key_file, 1, kKeysSize, k);
        std::fclose(k);
    }
    if (!im.walk(0, ""))
    {
        if (p.state.load() != 2)
            im.fail("The backup's file table is damaged.");
        return;
    }
    p.certificates = im.certificates();
    if (std::FILE *m = std::fopen((wii_root + kMarker).c_str(), "w"))
    {
        std::fprintf(m, "Imported by Porpoise from %s: %d files.\n", bin.c_str(), p.files);
        std::fclose(m);
    }
    char line[128];
    std::snprintf(line, sizeof line, "nand: %d files, %d of Porpoise's kept aside, certificates %d", p.files, p.kept,
                  p.certificates);
    ps5::debug::mark(line);
    p.state.store(1);
}
} // namespace porpoise::nand
