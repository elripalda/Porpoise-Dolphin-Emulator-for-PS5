/* Host checks for moving Porpoise's folder. No real player files are used.
 * SPDX-License-Identifier: GPL-3.0-or-later */
#include <atomic>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <dirent.h>
#include <filesystem>
#include <fstream>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

#include "porpoise_atomic.hpp"

namespace fs = std::filesystem;
enum class Fault { None, Read, Stat, Directory, Cancel, StaleErrno };
Fault fault = Fault::None;
bool injected = false, saw_entry = false;
std::string pointer_root;
#define PORPOISE_DATA pointer_root.c_str()

std::size_t fault_fread(void *, std::size_t, std::size_t, std::FILE *);
int fault_stat(const char *, struct stat *);
dirent *fault_readdir(DIR *);

// Extracted from porpoise_main.cpp by run.py, not a second mover implementation.
#include "mover.inc"

std::size_t fault_fread(void *data, std::size_t size, std::size_t count, std::FILE *stream)
{
    const std::size_t read = std::fread(data, size, count, stream);
    if (!injected && read > 0)
    {
        if (fault == Fault::Read)
        {
            // The next read encounters a real stdio error (EBADF), not EOF.
            ::close(fileno(stream));
            injected = true;
        }
        else if (fault == Fault::Cancel)
        {
            g_mover.stop = true;
            injected = true;
        }
    }
    if (fault == Fault::StaleErrno)
        errno = EIO; // Unrelated errno must not turn normal end-of-directory into failure.
    return read;
}

int fault_stat(const char *path, struct stat *st)
{
    if (fault == Fault::Stat && !injected)
    {
        injected = true;
        errno = EIO;
        return -1;
    }
    return ::stat(path, st);
}

dirent *fault_readdir(DIR *directory)
{
    if (fault == Fault::Directory && saw_entry && !injected)
    {
        injected = true;
        errno = EIO;
        return nullptr;
    }
    dirent *entry = ::readdir(directory);
    if (entry && entry->d_name[0] != '.')
        saw_entry = true;
    return entry;
}

void write_file(const fs::path &path, const std::string &contents)
{
    fs::create_directories(path.parent_path());
    std::ofstream file(path, std::ios::binary);
    file.write(contents.data(), contents.size());
    if (!file)
        std::abort();
}

std::string read_file(const fs::path &path)
{
    std::ifstream file(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

bool scenario(const fs::path &root, const char *name, Fault kind, bool nested = false)
{
    const fs::path fixture = root / name;
    pointer_root = (fixture / "pointer").string();
    g_mover.from = (fixture / "source").string();
    g_mover.to = (fixture / "destination").string();
    g_mover.state = 1;
    g_mover.stop = false;
    g_mover.done = g_mover.total = 0;
    g_mover.error.clear();
    g_mover.made_files.clear();
    g_mover.made_dirs.clear();
    fault = kind;
    injected = saw_entry = false;

    const std::string contents(8u << 20, 'S');
    const fs::path relative = nested ? "saves/User/GC/save.bin" : "save.bin";
    write_file(fs::path(g_mover.from) / relative, contents);
    if (nested)
    {
        fs::create_directories(fs::path(g_mover.from) / "empty-folder");
        write_file(fs::path(g_mover.from) / "empty-file", "");
    }
    const std::string original_pointer = g_mover.from + "\n";
    write_file(fs::path(pointer_root) / "location.txt", original_pointer);
    mover_worker(nullptr);

    const bool success = kind == Fault::None || kind == Fault::StaleErrno;
    bool ok;
    if (success)
    {
        ok = g_mover.state == 3 && !fs::exists(g_mover.from)
             && read_file(fs::path(g_mover.to) / relative) == contents
             && read_file(fs::path(pointer_root) / "location.txt") == g_mover.to + "\n";
        if (nested)
            ok = ok && fs::is_directory(fs::path(g_mover.to) / "empty-folder")
                 && fs::exists(fs::path(g_mover.to) / "empty-file")
                 && fs::file_size(fs::path(g_mover.to) / "empty-file") == 0;
    }
    else
    {
        ok = injected && g_mover.state == (kind == Fault::Cancel ? 5 : 4)
             && read_file(fs::path(g_mover.from) / relative) == contents
             && !fs::exists(g_mover.to)
             && read_file(fs::path(pointer_root) / "location.txt") == original_pointer;
    }
    std::printf("%s: %s (state %d)\n", name, ok ? "PASS" : "FAIL", int(g_mover.state));
    return ok;
}

int main(int argc, char **argv)
{
    if (argc != 2)
        return 2;
    const fs::path root = argv[1];
    bool ok = scenario(root, "normal", Fault::None);
    ok = scenario(root, "nested-and-empty", Fault::None, true) && ok;
    ok = scenario(root, "read-error", Fault::Read) && ok;
    ok = scenario(root, "stat-error", Fault::Stat) && ok;
    ok = scenario(root, "directory-error", Fault::Directory) && ok;
    ok = scenario(root, "cancel", Fault::Cancel) && ok;
    ok = scenario(root, "stale-errno", Fault::StaleErrno) && ok;
    return ok ? 0 : 1;
}
