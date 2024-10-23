#include <sys/mman.h>
#include <sys/syscall.h>
#include <sys/mount.h>
#include <map>
#include <utility>

#include "main.hpp"
#include "base.hpp"
#include "node.hpp"

using namespace std;

#define VLOGD(tag, from, to) LOGD("%-8s: %s <- %s", tag, to, from)

static int bind_mount(const char *reason, const char *from, const char *to, bool move = false) {
    int ret = xmount(from, to, nullptr, (move ? MS_MOVE : MS_BIND) | MS_REC, nullptr);
    if (ret == 0)
        VLOGD(reason, from, to);
    return ret;
}

/*************************
 * Node Tree Construction
 *************************/

tmpfs_node::tmpfs_node(node_entry *node) : dir_node(node, this) {
    if (!replace()) {
        if (auto dir = open_dir(node_path().data())) {
            set_exist(true);
            for (dirent *entry; (entry = xreaddir(dir.get()));) {
                // create a dummy inter_node to upgrade later
                emplace<inter_node>(entry->d_name, entry);
            }
        }
    }

    for (auto it = children.begin(); it != children.end(); ++it) {
        // Upgrade resting inter_node children to tmpfs_node
        if (isa<inter_node>(it->second))
            it = upgrade<tmpfs_node>(it);
    }
}

bool dir_node::prepare() {
    // If direct replace or not exist, mount ourselves as tmpfs
    bool upgrade_to_tmpfs = replace() || !exist();

    for (auto it = children.begin(); it != children.end();) {
        // We also need to upgrade to tmpfs node if any child:
        // - Target does not exist
        // - Source or target is a symlink (since we cannot bind mount symlink)
        bool cannot_mnt;
        if (struct stat st{}; lstat(it->second->node_path().data(), &st) != 0) {
            cannot_mnt = true;
        } else {
            it->second->set_exist(true);
            cannot_mnt = it->second->is_lnk() || S_ISLNK(st.st_mode);
        }

        if (cannot_mnt) {
            if (_node_type > type_id<tmpfs_node>()) {
                // Upgrade will fail, remove the unsupported child node
                LOGW("Unable to add: %s, skipped", it->second->node_path().data());
                delete it->second;
                it = children.erase(it);
                continue;
            }
            upgrade_to_tmpfs = true;
        }
        if (auto dn = dyn_cast<dir_node>(it->second)) {
            if (replace()) {
                // Propagate skip mirror state to all children
                dn->set_replace(true);
            }
            if (dn->prepare()) {
                // Upgrade child to tmpfs
                it = upgrade<tmpfs_node>(it);
            }
        }
        ++it;
    }
    return upgrade_to_tmpfs;
}

void dir_node::collect_module_files(const char *module, int dfd) {
    LOGD("collect %s: %s", module, peek_node_path().data());
    auto dir = xopen_dir(xopenat(dfd, name().data(), O_RDONLY | O_CLOEXEC));
    if (!dir)
        return;

    for (dirent *entry; (entry = xreaddir(dir.get()));) {
        if (entry->d_name == ".replace"sv) {
            set_replace(true);
            continue;
        }

        if (entry->d_type == DT_DIR) {
            inter_node *node;
            if (auto it = children.find(entry->d_name); it == children.end()) {
                node = emplace<inter_node>(entry->d_name, entry->d_name);
            } else {
                node = dyn_cast<inter_node>(it->second);
            }
            if (node) {
                node->collect_module_files(module, dirfd(dir.get()));
            }
        } else {
            emplace<module_node>(entry->d_name, module, entry);
        }
    }
}

/************************
 * Mount Implementations
 ************************/

void node_entry::create_and_mount(const char *reason, const string &src, bool ro) {
    const string dest = isa<tmpfs_node>(parent()) ? worker_path() : node_path();
    if (is_lnk()) {
        VLOGD("cp_link", src.data(), dest.data());
        cp_afc(src.data(), dest.data());
    } else {
        if (is_dir())
            xmkdir(dest.data(), 0);
        else if (is_reg())
            close(xopen(dest.data(), O_RDONLY | O_CREAT | O_CLOEXEC, 0));
        else
            return;
        bind_mount(reason, src.data(), dest.data());
        if (ro) {
            xmount(nullptr, dest.data(), nullptr, MS_REMOUNT | MS_BIND | MS_RDONLY, nullptr);
        }
    }
}

void module_node::mount() {
    std::string path = module + (parent()->root()->prefix + node_path());
    string mnt_src = module_mnt + path;
    {
        string src = MODULEROOT "/" + path;
        if (exist()) clone_attr(node_path().data(), src.data());
    }
    if (isa<tmpfs_node>(parent())) {
        create_and_mount("module", mnt_src);
    } else {
        bind_mount("module", mnt_src.data(), node_path().data());
    }
}

void tmpfs_node::mount() {
    if (!is_dir()) {
        create_and_mount("mirror", node_path());
        return;
    }
    if (!isa<tmpfs_node>(parent())) {
        auto worker_dir = worker_path();
        mkdirs(worker_dir.data(), 0);
        bind_mount(replace() ? "replace" : "move", worker_dir.data(), worker_dir.data());
        clone_attr(exist() ? node_path().data() : parent()->node_path().data(), worker_dir.data());
        dir_node::mount();
        bind_mount(replace() ? "replace" : "move", worker_dir.data(), node_path().data(), true);
        xmount(nullptr, node_path().data(), nullptr, MS_REMOUNT | MS_RDONLY, nullptr);
        xmount(nullptr, node_path().data(), nullptr, MS_PRIVATE, nullptr);
    } else {
        const string dest = worker_path();
        // We don't need another layer of tmpfs if parent is tmpfs
        mkdir(dest.data(), 0);
        clone_attr(exist() ? node_path().data() : parent()->worker_path().data(), dest.data());
        dir_node::mount();
    }
}

void load_modules(const vector<module_info> &module_list) {
    node_entry::module_mnt = MODULEROOT "/";

    auto root = make_unique<root_node>("");
    auto system = new root_node("system");
    root->insert(system);

    char buf[4096];
    LOGI("* Loading modules");
    for (const auto &m: module_list) {
        const char *module = m.name.data();
        char *b = buf + snprintf(buf, sizeof(buf), MODULEROOT "/%s/", module);

        // Check whether skip mounting
        strcpy(b, "skip_mount");
        if (access(buf, F_OK) == 0)
            continue;

        // Double check whether the system folder exists
        strcpy(b, "system");
        if (access(buf, F_OK) != 0)
            continue;

        LOGI("%s: loading mount files", module);
        b[-1] = '\0';
        int fd = xopen(buf, O_RDONLY | O_CLOEXEC);
        system->collect_module_files(module, fd);
        close(fd);
    }

    if (!system->is_empty()) {
        // Handle special read-only partitions
        for (auto &part: partitions) {
            struct stat st{};
            if (!part.empty() && lstat(part.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) {
                if (auto old = system->extract(part.c_str() + 1)) {
                    auto new_node = new root_node(old);
                    root->insert(new_node);
                }
            }
        }
        root->prepare();
        root->mount();
    }
}

template<typename Func>
static void foreach_module(Func fn) {
    auto dir = open_dir(MODULEROOT);
    if (!dir)
        return;

    int dfd = dirfd(dir.get());
    for (dirent *entry; (entry = xreaddir(dir.get()));) {
        if (entry->d_type == DT_DIR && entry->d_name != ".core"sv) {
            int modfd = xopenat(dfd, entry->d_name, O_RDONLY | O_CLOEXEC);
            fn(dfd, entry, modfd);
            close(modfd);
        }
    }
}

void handle_modules() {
    vector<module_info> module_list;
    LOGD("collecting modules ...");
    foreach_module([&](int dfd, dirent *entry, int modfd) {
        // unlinkat(modfd, "update", 0);
        if (faccessat(modfd, "disable", F_OK, 0) == 0)
            return;

        module_info info;
        info.name = entry->d_name;
        module_list.push_back(info);
    });
    LOGD("loading modules ...");
    load_modules(module_list);
}

void umount_modules(const char *magic) {
    vector<string> targets;
    for (auto &info: parse_mount_info("self")) {
        if (info.root.starts_with("/adb/modules/") ||
            (info.source == magic && info.type == "tmpfs")) {
            targets.emplace_back(info.target);
        }
    }

    for (auto &target: targets) {
        if (umount2(target.c_str(), MNT_DETACH) == -1) {
            PLOGE("umount %s", target.c_str());
        } else {
            LOGD("umount %s", target.c_str());
        }
    }
}
