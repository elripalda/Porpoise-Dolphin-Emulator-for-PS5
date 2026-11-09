/* Regression checks for preserving existing files during NAND import.
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Exercise the importer's real filesystem operations with empty incoming
 * files. Decryption and PS5 logging are not needed for these checks. */
#include "../../src/porpoise_nand.cpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace ps5::debug
{
void mark(const char *) noexcept {}
}
namespace porpoise::disc
{
void aes_cbc_decrypt(const std::uint8_t[16], std::uint8_t[16], std::uint8_t *, std::size_t)
{
    throw std::runtime_error("these filesystem tests must not decrypt a cluster");
}
}

namespace
{
namespace fs = std::filesystem;
int failures = 0;

void check(bool ok, const char *message)
{
    if (!ok)
    {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

void write(const fs::path &path, const std::string &contents)
{
    std::ofstream file(path, std::ios::binary);
    file << contents;
    file.close();
    if (!file)
        throw std::runtime_error("could not write fixture: " + path.string());
}

std::string read(const fs::path &path)
{
    std::ifstream file(path, std::ios::binary);
    if (!file)
        throw std::runtime_error("could not read fixture: " + path.string());
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

void scenario(const fs::path &base, bool blocked, bool exhausted, bool existing)
{
    fs::create_directories(base / "wii");
    if (existing)
        write(base / "wii/save.dat", "original save");
    if (blocked)
        write(base / "kept", "not a directory");
    else if (exhausted)
    {
        fs::create_directories(base / "kept");
        write(base / "kept/save.dat", "earlier backup");
        for (int i = 2; i < 100; ++i)
            write(base / "kept" / ("save.dat." + std::to_string(i)), "earlier backup");
    }

    porpoise::nand::Progress progress;
    porpoise::nand::Importer importer;
    importer.root = (base / "wii").string();
    importer.kept_root = (base / "kept").string();
    importer.p = &progress;
    porpoise::nand::Entry incoming; /* an empty file is a valid NAND entry */
    const bool ok = importer.file(incoming, "/save.dat");

    if (blocked || exhausted)
    {
        check(!ok, "import must fail when the existing file cannot be kept");
        check(read(base / "wii/save.dat") == "original save", "failed preservation must leave the original intact");
        check(progress.kept == 0, "failed preservation must not count a kept file");
        if (exhausted)
        {
            check(read(base / "kept/save.dat") == "earlier backup", "earlier backup must remain intact");
            for (int i = 2; i < 100; ++i)
                check(read(base / "kept" / ("save.dat." + std::to_string(i))) == "earlier backup",
                      "earlier numbered backup must remain intact");
        }
    }
    else
    {
        check(ok, "a writable destination must accept the incoming file");
        check(read(base / "wii/save.dat").empty(), "the incoming file must be installed");
        check(progress.kept == (existing ? 1 : 0), "kept count must match the original files preserved");
        if (existing)
            check(read(base / "kept/save.dat") == "original save", "the original must be preserved before replacement");
    }
}
}

int main(int argc, char **argv)
{
    if (argc != 2)
        return 2;
    try
    {
        const fs::path base = argv[1];
        scenario(base / "replacement", false, false, true);
        scenario(base / "blocked-backup", true, false, true);
        scenario(base / "full-backup-names", false, true, true);
        scenario(base / "new-file", false, false, false);
    }
    catch (const std::exception &e)
    {
        std::cerr << "Fixture failure: " << e.what() << '\n';
        return 2;
    }
    if (failures)
        return 1;
    std::cout << "NAND preservation checks passed\n";
    return 0;
}
