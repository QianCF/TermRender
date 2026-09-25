#pragma once
#include "scene.h"
#include <string>

namespace tr {

// Decode PNG/JPEG/BMP/TGA/... from memory into an RGBA8 Image.
bool decode_image_memory(const unsigned char *data, size_t size, Image &out);

// Decode an image file from disk.
bool load_image_file(const std::string &path, Image &out);

} // namespace tr
