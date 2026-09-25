#pragma once
#include "scene.h"
#include <string>

namespace tr {

// Load a .gltf or .glb file into a Scene. Returns false and fills `error`
// on failure.
bool load_gltf(const std::string &path, Scene &scene, std::string &error);

} // namespace tr
