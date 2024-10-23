#include <sys/mount.h>
#include <sys/xattr.h>
#include <sys/syscall.h>
#include <sys/sendfile.h>
#include <sys/sysmacros.h>

#include "base.hpp"
#include "logging.h"

using namespace std::string_view_literals;

struct dirent *xreaddir(DIR *dirp) {
    errno = 0;
    for (dirent *e;;) {
        e = readdir(dirp);
        if (e == nullptr) {
            if (errno)
                PLOGE("readdir");
            return nullptr;
        } else if (e->d_name == "."sv || e->d_name == ".."sv) {
            // Filter . and .. for users
            continue;
        }
        return e;
    }
}

sDIR make_dir(DIR *dp) {
    return sDIR(dp, [](DIR *dp) { return dp ? closedir(dp) : 1; });
}

sFILE make_file(FILE *fp) {
    return sFILE(fp, [](FILE *fp) { return fp ? fclose(fp) : 1; });
}

int xmount(const char *source, const char *target,
           const char *filesystemtype, unsigned long mountflags,
           const void *data) {
    int ret = mount(source, target, filesystemtype, mountflags, data);
    if (ret < 0) {
        PLOGE("mount %s->%s", source, target);
    }
    return ret;
}


struct file_attr {
    struct stat st;
    char con[128];
};

static void freecon(char *s) {
    free(s);
}

static int getfilecon(const char *path, char **ctx) {
    char buf[1024];
    int rc = syscall(__NR_getxattr, path, XATTR_NAME_SELINUX, buf, sizeof(buf) - 1);
    if (rc >= 0)
        *ctx = strdup(buf);
    return rc;
}

static int lgetfilecon(const char *path, char **ctx) {
    char buf[1024];
    int rc = syscall(__NR_lgetxattr, path, XATTR_NAME_SELINUX, buf, sizeof(buf) - 1);
    if (rc >= 0)
        *ctx = strdup(buf);
    return rc;
}

static int fgetfilecon(int fd, char **ctx) {
    char buf[1024];
    int rc = syscall(__NR_fgetxattr, fd, XATTR_NAME_SELINUX, buf, sizeof(buf) - 1);
    if (rc >= 0)
        *ctx = strdup(buf);
    return rc;
}

static int setfilecon(const char *path, const char *ctx) {
    return syscall(__NR_setxattr, path, XATTR_NAME_SELINUX, ctx, strlen(ctx) + 1, 0);
}

static int lsetfilecon(const char *path, const char *ctx) {
    return syscall(__NR_lsetxattr, path, XATTR_NAME_SELINUX, ctx, strlen(ctx) + 1, 0);
}

static int fsetfilecon(int fd, const char *ctx) {
    return syscall(__NR_fsetxattr, fd, XATTR_NAME_SELINUX, ctx, strlen(ctx) + 1, 0);
}

ssize_t fd_path(int fd, char *path, size_t size) {
    snprintf(path, size, "/proc/self/fd/%d", fd);
    return xreadlink(path, path, size);
}

int fd_pathat(int dirfd, const char *name, char *path, size_t size) {
    if (fd_path(dirfd, path, size) < 0)
        return -1;
    auto len = strlen(path);
    path[len] = '/';
    strlcpy(path + len + 1, name, size - len - 1);
    return 0;
}

int getattr(const char *path, file_attr *a) {
    if (xlstat(path, &a->st) == -1)
        return -1;
    char *con;
    if (lgetfilecon(path, &con) == -1)
        return -1;
    strcpy(a->con, con);
    freecon(con);
    return 0;
}

int getattrat(int dirfd, const char *name, file_attr *a) {
    char path[4096];
    fd_pathat(dirfd, name, path, sizeof(path));
    return getattr(path, a);
}

int fgetattr(int fd, file_attr *a) {
    if (xfstat(fd, &a->st) < 0)
        return -1;
    char *con;
    if (fgetfilecon(fd, &con) < 0)
        return -1;
    strcpy(a->con, con);
    freecon(con);
    return 0;
}


int setattr(const char *path, file_attr *a) {
    if (chmod(path, a->st.st_mode & 0777) < 0)
        return -1;
    if (chown(path, a->st.st_uid, a->st.st_gid) < 0)
        return -1;
    if (a->con[0] && lsetfilecon(path, a->con) < 0)
        return -1;
    return 0;
}

int setattrat(int dirfd, const char *name, file_attr *a) {
    char path[4096];
    fd_pathat(dirfd, name, path, sizeof(path));
    return setattr(path, a);
}

int fsetattr(int fd, file_attr *a) {
    if (fchmod(fd, a->st.st_mode & 0777) < 0)
        return -1;
    if (fchown(fd, a->st.st_uid, a->st.st_gid) < 0)
        return -1;
    if (a->con[0] && fsetfilecon(fd, a->con) < 0)
        return -1;
    return 0;
}

void clone_attr(const char *src, const char *dest) {
    file_attr a;
    getattr(src, &a);
    setattr(dest, &a);
}

void fclone_attr(int src, int dest) {
    file_attr a;
    fgetattr(src, &a);
    fsetattr(dest, &a);
}

// https://github.com/topjohnwu/Magisk/blob/66a7ef5615f463435b45d29e737d37cf48a9b78c/native/src/base/files.cpp#L26

int mkdirs(const char *path, mode_t mode) {
    char buf[4096];
    strlcpy(buf, path, sizeof(buf));
    errno = 0;
    for (char *p = &buf[1]; *p; ++p) {
        if (*p == '/') {
            *p = '\0';
            if (mkdir(buf, mode) == -1) {
                if (errno != EEXIST)
                    return -1;
            }
            *p = '/';
        }
    }
    if (mkdir(buf, mode) == -1) {
        if (errno != EEXIST)
            return -1;
    }
    return 0;
}

int xmkdirs(const char *pathname, mode_t mode) {
    int ret = mkdirs(pathname, mode);
    if (ret < 0) {
        PLOGE("mkdirs %s", pathname);
    }
    return ret;
}

// https://github.com/topjohnwu/Magisk/blob/40aab136019f4c1950f0789baf92a0686cd0a29e/native/src/base/files.cpp#L144


void clone_dir(int src, int dest) {
    auto dir = xopen_dir(src);
    run_finally f([&] { close(dest); });
    for (dirent *entry; (entry = xreaddir(dir.get()));) {
        file_attr a;
        getattrat(src, entry->d_name, &a);
        switch (entry->d_type) {
            case DT_DIR: {
                xmkdirat(dest, entry->d_name, 0);
                setattrat(dest, entry->d_name, &a);
                int sfd = xopenat(src, entry->d_name, O_RDONLY | O_CLOEXEC);
                int dst = xopenat(dest, entry->d_name, O_RDONLY | O_CLOEXEC);
                clone_dir(sfd, dst);
                break;
            }
            case DT_REG: {
                int sfd = xopenat(src, entry->d_name, O_RDONLY | O_CLOEXEC);
                int dfd = xopenat(dest, entry->d_name, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0);
                xsendfile(dfd, sfd, nullptr, a.st.st_size);
                fsetattr(dfd, &a);
                close(dfd);
                close(sfd);
                break;
            }
            case DT_LNK: {
                char buf[4096];
                xreadlinkat(src, entry->d_name, buf, sizeof(buf));
                xsymlinkat(buf, dest, entry->d_name);
                setattrat(dest, entry->d_name, &a);
                break;
            }
        }
    }
}

void cp_afc(const char *src, const char *dest) {
    file_attr a;
    getattr(src, &a);
    if (S_ISDIR(a.st.st_mode)) {
        xmkdirs(dest, 0);
        clone_dir(xopen(src, O_RDONLY | O_CLOEXEC), xopen(dest, O_RDONLY | O_CLOEXEC));
    } else {
        unlink(dest);
        if (S_ISREG(a.st.st_mode)) {
            int sfd = xopen(src, O_RDONLY | O_CLOEXEC);
            int dfd = xopen(dest, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0);
            xsendfile(dfd, sfd, nullptr, a.st.st_size);
            close(sfd);
            close(dfd);
        } else if (S_ISLNK(a.st.st_mode)) {
            char buf[4096];
            xreadlink(src, buf, sizeof(buf));
            xsymlink(buf, dest);
        }
    }
    setattr(dest, &a);
}

void file_readline(bool trim, FILE *fp, const std::function<bool(std::string_view)> &fn) {
    size_t len = 1024;
    char *buf = (char *) malloc(len);
    char *start;
    ssize_t read;
    while ((read = getline(&buf, &len, fp)) >= 0) {
        start = buf;
        if (trim) {
            while (read && "\n\r "sv.find(buf[read - 1]) != std::string::npos)
                --read;
            buf[read] = '\0';
            while (*start == ' ')
                ++start;
        }
        if (!fn(start))
            break;
    }
    free(buf);
}

void file_readline(bool trim, const char *file, const std::function<bool(std::string_view)> &fn) {
    if (auto fp = open_file(file, "re"))
        file_readline(trim, fp.get(), fn);
}

void file_readline(const char *file, const std::function<bool(std::string_view)> &fn) {
    file_readline(false, file, fn);
}

int parse_int(std::string_view s) {
    if (s.empty()) return -1;
    int val = 0;
    for (char c: s) {
        if (!c) break;
        if (c > '9' || c < '0')
            return -1;
        val = val * 10 + c - '0';
    }
    return val;
}

std::vector<mount_info> parse_mount_info(const char *pid) {
    char buf[PATH_MAX] = {};
    snprintf(buf, sizeof(buf), "/proc/%s/mountinfo", pid);
    std::vector<mount_info> result;

    file_readline(buf, [&result](std::string_view line) -> bool {
        int root_start = 0, root_end = 0;
        int target_start = 0, target_end = 0;
        int vfs_option_start = 0, vfs_option_end = 0;
        int type_start = 0, type_end = 0;
        int source_start = 0, source_end = 0;
        int fs_option_start = 0, fs_option_end = 0;
        int optional_start = 0, optional_end = 0;
        unsigned int id, parent, maj, min;
        sscanf(line.data(),
               "%u "           // (1) id
               "%u "           // (2) parent
               "%u:%u "        // (3) maj:min
               "%n%*s%n "      // (4) mountroot
               "%n%*s%n "      // (5) target
               "%n%*s%n"       // (6) vfs options (fs-independent)
               "%n%*[^-]%n - " // (7) optional fields
               "%n%*s%n "      // (8) FS type
               "%n%*s%n "      // (9) source
               "%n%*s%n",      // (10) fs options (fs specific)
               &id, &parent, &maj, &min, &root_start, &root_end, &target_start,
               &target_end, &vfs_option_start, &vfs_option_end,
               &optional_start, &optional_end, &type_start, &type_end,
               &source_start, &source_end, &fs_option_start, &fs_option_end);

        auto root = line.substr(root_start, root_end - root_start);
        auto target = line.substr(target_start, target_end - target_start);
        auto vfs_option =
                line.substr(vfs_option_start, vfs_option_end - vfs_option_start);
        ++optional_start;
        --optional_end;
        auto optional = line.substr(
                optional_start,
                optional_end - optional_start > 0 ? optional_end - optional_start : 0);

        auto type = line.substr(type_start, type_end - type_start);
        auto source = line.substr(source_start, source_end - source_start);
        auto fs_option =
                line.substr(fs_option_start, fs_option_end - fs_option_start);

        unsigned int shared = 0;
        unsigned int master = 0;
        unsigned int propagate_from = 0;
        if (auto pos = optional.find("shared:"); pos != std::string_view::npos) {
            shared = parse_int(optional.substr(pos + 7));
        }
        if (auto pos = optional.find("master:"); pos != std::string_view::npos) {
            master = parse_int(optional.substr(pos + 7));
        }
        if (auto pos = optional.find("propagate_from:");
                pos != std::string_view::npos) {
            propagate_from = parse_int(optional.substr(pos + 15));
        }

        result.emplace_back(mount_info{
                .id = id,
                .parent = parent,
                .device = static_cast<dev_t>(makedev(maj, min)),
                .root {root},
                .target {target},
                .vfs_option {vfs_option},
                .optional {
                        .shared = shared,
                        .master = master,
                        .propagate_from = propagate_from,
                },
                .type {type},
                .source {source},
                .fs_option {fs_option},
        });
        return true;
    });
    return result;
}

int xsymlink(const char *target, const char *linkpath) {
    int ret = symlink(target, linkpath);
    if (ret < 0) {
        PLOGE("symlink %s->%s", target, linkpath);
    }
    return ret;
}

ssize_t xreadlink(const char *pathname, char *buf, size_t bufsiz) {
    ssize_t ret = readlink(pathname, buf, bufsiz);
    if (ret < 0) {
        PLOGE("readlink %s", pathname);
    } else {
        buf[ret] = '\0';
    }
    return ret;
}

ssize_t xreadlinkat(int dirfd, const char *pathname, char *buf, size_t bufsiz) {
    // readlinkat() may fail on x86 platform, returning random value
    // instead of number of bytes placed in buf (length of link)
#if defined(__i386__) || defined(__x86_64__)
    memset(buf, 0, bufsiz);
    ssize_t ret = readlinkat(dirfd, pathname, buf, bufsiz);
    if (ret < 0) {
        PLOGE("readlinkat %s", pathname);
    }
    return ret;
#else
    ssize_t ret = readlinkat(dirfd, pathname, buf, bufsiz);
    if (ret < 0) {
        PLOGE("readlinkat %s", pathname);
    } else {
        buf[ret] = '\0';
    }
    return ret;
#endif
}


int xsymlinkat(const char *target, int newdirfd, const char *linkpath) {
    int ret = symlinkat(target, newdirfd, linkpath);
    if (ret < 0) {
        PLOGE("symlinkat %s->%s", target, linkpath);
    }
    return ret;
}

ssize_t xsendfile(int out_fd, int in_fd, off_t *offset, size_t count) {
    ssize_t ret = sendfile(out_fd, in_fd, offset, count);
    if (ret < 0) {
        PLOGE("sendfile");
    }
    return ret;
}

int xlstat(const char *pathname, struct stat *buf) {
    int ret = lstat(pathname, buf);
    if (ret < 0) {
        PLOGE("lstat %s", pathname);
    }
    return ret;
}

int xfstat(int fd, struct stat *buf) {
    int ret = fstat(fd, buf);
    if (ret < 0) {
        PLOGE("fstat %d", fd);
    }
    return ret;
}

int xmkdirat(int dirfd, const char *pathname, mode_t mode) {
    int ret = mkdirat(dirfd, pathname, mode);
    if (ret < 0 && errno != EEXIST) {
        PLOGE("mkdirat %s %u", pathname, mode);
    }
    return ret;
}

int xmkdir(const char *pathname, mode_t mode) {
    int ret = mkdir(pathname, mode);
    if (ret < 0 && errno != EEXIST) {
        PLOGE("mkdir %s %u", pathname, mode);
    }
    return ret;
}

int xopen(const char *pathname, int flags) {
    int fd = open(pathname, flags);
    if (fd < 0) {
        PLOGE("open: %s", pathname);
    }
    return fd;
}

int xopen(const char *pathname, int flags, mode_t mode) {
    int fd = open(pathname, flags, mode);
    if (fd < 0) {
        PLOGE("open: %s", pathname);
    }
    return fd;
}

int xopenat(int dirfd, const char *pathname, int flags) {
    int fd = openat(dirfd, pathname, flags);
    if (fd < 0) {
        PLOGE("openat: %s", pathname);
    }
    return fd;
}

int xopenat(int dirfd, const char *pathname, int flags, mode_t mode) {
    int fd = openat(dirfd, pathname, flags, mode);
    if (fd < 0) {
        PLOGE("openat: %s", pathname);
    }
    return fd;
}
