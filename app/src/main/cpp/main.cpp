#include <sys/mount.h>

#include "main.hpp"
#include "logging.h"

std::string tmp_path = "/debug_ramdisk";

using namespace std::string_view_literals;

std::vector<std::string> partitions{"/vendor", "/product", "/system_ext"};

void help() {
    LOGE("usage: magic_mount <mount|umount> [--work-dir dir] [--magic magic] [--add-partitions /p1,/p2,....]");
}

int main(int argc, char **argv) {
#ifndef NDEBUG
    logging::setPrintEnabled(true);
#endif

    const char *magic = "magic";

    if (argc < 2) {
        help();
        return 1;
    }

    bool do_umount = false;

    if (argv[1] == "umount"sv) {
        do_umount = true;
    } else if (argv[1] != "mount"sv) {
        help();
        return 1;
    }

    for (int i = 2; i < argc; i++) {
        if (argv[i] == "--work-dir"sv && i + 1 < argc) {
            tmp_path = argv[i + 1];
        } else if (argv[i] == "--magic"sv && i + 1 < argc) {
            magic = argv[i + 1];
        } else if (argv[i] == "--add-partitions"sv && i + 1 < argc) {
            size_t pos = 0;
            std::string_view ps{argv[i + 1]};
            for (;;) {
                auto new_pos = ps.find(',', pos);
                if (new_pos != std::string_view::npos) {
                    partitions.emplace_back(ps.substr(pos, new_pos - pos));
                    pos = new_pos + 1;
                    continue;
                }
                break;
            }
            partitions.emplace_back(ps.substr(pos));
        }
    }

    if (do_umount) {
        umount_modules(magic);
        return 0;
    }

    LOGI("magic_mount: work dir %s magic %s", tmp_path.c_str(), magic);
    for (auto &s: partitions) {
        LOGD("supported partitions: %s", s.c_str());
    }

    if (mount(magic, tmp_path.c_str(), "tmpfs", 0, nullptr) == -1) {
        PLOGE("mount tmp");
        return 1;
    }
    if (mount(nullptr, tmp_path.c_str(), nullptr, MS_PRIVATE, nullptr) == -1) {
        PLOGE("mount tmp private");
        return 1;
    }
    handle_modules();
    if (umount2(tmp_path.c_str(), MNT_DETACH) == -1) {
        PLOGE("umount tmp");
    }
    return 0;
}

std::string get_magisk_tmp() {
    return tmp_path;
}
