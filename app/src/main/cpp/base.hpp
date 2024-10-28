#pragma once

#include <unistd.h>
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <cstring>

#include <memory>
#include <functional>
#include <string>
#include <vector>

#include "logging.h"

// https://github.com/topjohnwu/Magisk/blob/455b13b83c4dde60511e43a634c880317b1ba5fc/native/src/core/include/core.hpp#L31
struct module_info {
    std::string name;
};

// https://github.com/topjohnwu/Magisk/blob/455b13b83c4dde60511e43a634c880317b1ba5fc/native/src/include/consts.hpp#L8

#define SECURE_DIR      "/data/adb"
#define MODULEROOT      SECURE_DIR "/modules"

#define INTLROOT      ".magisk"
#define WORKERDIR     INTLROOT "/worker"
#define MODULEMNT     INTLROOT "/modules"

struct dirent *xreaddir(DIR *dirp);

// files

using sFILE = std::unique_ptr<FILE, decltype(&fclose)>;
using sDIR = std::unique_ptr<DIR, decltype(&closedir)>;

sDIR make_dir(DIR *dp);

sFILE make_file(FILE *fp);

static inline sDIR open_dir(const char *path) {
    return make_dir(opendir(path));
}

static inline sDIR xopen_dir(const char *path) {
    return make_dir(opendir(path));
}

static inline sDIR xopen_dir(int dirfd) {
    return make_dir(fdopendir(dirfd));
}

static inline sFILE open_file(const char *path, const char *mode) {
    return make_file(fopen(path, mode));
}

static inline sFILE xopen_file(const char *path, const char *mode) {
    return make_file(fopen(path, mode));
}

static inline sFILE xopen_file(int fd, const char *mode) {
    return make_file(fdopen(fd, mode));
}

#define DISALLOW_COPY_AND_MOVE(clazz) \
clazz(const clazz&) = delete;        \
clazz(clazz &&) = delete;

template<class Func>
class run_finally {
    DISALLOW_COPY_AND_MOVE(run_finally)

public:
    explicit run_finally(Func &&fn) : fn(std::move(fn)) {}

    ~run_finally() { fn(); }

private:
    Func fn;
};

void cp_afc(const char *src, const char *dest);

void clone_attr(const char *src, const char *dest);

int mkdirs(const char *path, mode_t mode);
int xmkdirs(const char *path, mode_t mode);

// mount scan

struct mount_info {
    unsigned int id;
    unsigned int parent;
    dev_t device;
    std::string root;
    std::string target;
    std::string vfs_option;
    struct {
        unsigned int shared;
        unsigned int master;
        unsigned int propagate_from;
    } optional;
    std::string type;
    std::string source;
    std::string fs_option;
};

std::vector<mount_info> parse_mount_info(const char *pid);

// https://github.com/topjohnwu/Magisk/blob/40aab136019f4c1950f0789baf92a0686cd0a29e/native/src/base/xwrap.cpp#L354
// xwrap

int xmount(const char *source, const char *target,
           const char *filesystemtype, unsigned long mountflags,
           const void *data);

int xsymlink(const char *target, const char *linkpath);
int xsymlinkat(const char *target, int newdirfd, const char *linkpath);

ssize_t xreadlink(const char *pathname, char *buf, size_t bufsiz);
ssize_t xreadlinkat(int dirfd, const char *pathname, char *buf, size_t bufsiz);

ssize_t xsendfile(int out_fd, int in_fd, off_t *offset, size_t count);

int xlstat(const char *pathname, struct stat *buf);
int xfstat(int fd, struct stat *buf);
int xmkdirat(int dirfd, const char *pathname, mode_t mode);
int xmkdir(const char *pathname, mode_t mode);

int xopen(const char *pathname, int flags);
int xopen(const char *pathname, int flags, mode_t mode);
int xopenat(int dirfd, const char *pathname, int flags);
int xopenat(int dirfd, const char *pathname, int flags, mode_t mode);
