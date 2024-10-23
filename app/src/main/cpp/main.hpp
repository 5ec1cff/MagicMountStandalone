#pragma once

#include <string>
#include <vector>

std::string get_magisk_tmp();

void handle_modules();

void umount_modules(const char *magic);

extern std::vector<std::string> partitions;
