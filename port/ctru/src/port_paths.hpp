#pragma once
#include <string>
// Where the 3DS device prefixes live on the console (MKW_SDMC / MKW_ROMFS
// override; default <exe dir>/sdmc and <exe dir>/romfs).
namespace port {
const std::string& sdmcRoot();
const std::string& romfsRoot();
const char* translate(const char* path, std::string& storage);
}
