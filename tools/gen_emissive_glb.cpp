// Dev-only generator for `assets/emissive_test.glb` (not part of the build).
//
// Scene: a diffuse ground plane lit ONLY by an emissive panel above it (no
// punctual lights, no environment). Used to verify that emissive materials
// illuminate the scene (emissive-triangle NEE in src/renderer.cpp).
//
//   g++ -O2 -std=c++17 tools/gen_emissive_glb.cpp -o gen_emissive_glb
//   ./gen_emissive_glb assets/emissive_test.glb

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

struct Buf {
    std::vector<uint8_t> data;
    size_t add(const void *p, size_t n) {
        size_t off = data.size();
        data.insert(data.end(), (const uint8_t *)p, (const uint8_t *)p + n);
        while (data.size() % 4) data.push_back(0);
        return off;
    }
};

static void push3(std::vector<float> &v, float x, float y, float z) {
    v.push_back(x);
    v.push_back(y);
    v.push_back(z);
}

int main(int argc, char **argv) {
    const char *outpath = argc > 1 ? argv[1] : "emissive_test.glb";

    Buf buf;
    std::ostringstream acc, bview;
    int acc_count = 0, bview_count = 0;

    // Ground quad (10x10 at y=0), normal +Y.
    std::vector<float> gpP = {-5, 0, -5, 5, 0, -5, 5, 0, 5, -5, 0, 5};
    std::vector<float> gpN(12, 0.0f);
    for (int i = 0; i < 4; i++) gpN[i * 3 + 1] = 1.0f;
    std::vector<uint32_t> gpI = {0, 2, 1, 0, 3, 2};

    // Emissive panel (1x1 at y=2), normal -Y (facing the ground).
    std::vector<float> epP = {-0.5f, 2.0f, -0.5f, -0.5f, 2.0f, 0.5f,
                              0.5f,  2.0f, 0.5f,  0.5f,  2.0f, -0.5f};
    std::vector<float> epN(12, 0.0f);
    for (int i = 0; i < 4; i++) epN[i * 3 + 1] = -1.0f;
    std::vector<uint32_t> epI = {0, 1, 2, 0, 2, 3};

    auto add_f = [&](const std::vector<float> &v, const char *type, int comps, const float *mn,
                     const float *mx) -> int {
        size_t off = buf.add(v.data(), v.size() * 4);
        int bi = bview_count++;
        bview << (bi ? "," : "") << "{\"buffer\":0,\"byteOffset\":" << off
              << ",\"byteLength\":" << v.size() * 4 << "}";
        int ai = acc_count++;
        acc << (ai ? "," : "") << "{\"bufferView\":" << bi
            << ",\"componentType\":5126,\"count\":" << (v.size() / comps) << ",\"type\":\"" << type
            << "\"";
        if (mn && mx)
            acc << ",\"min\":[" << mn[0] << "," << mn[1] << "," << mn[2] << "],\"max\":[" << mx[0]
                << "," << mx[1] << "," << mx[2] << "]";
        acc << "}";
        return ai;
    };
    auto add_i = [&](const std::vector<uint32_t> &v) -> int {
        size_t off = buf.add(v.data(), v.size() * 4);
        int bi = bview_count++;
        bview << (bi ? "," : "") << "{\"buffer\":0,\"byteOffset\":" << off
              << ",\"byteLength\":" << v.size() * 4 << "}";
        int ai = acc_count++;
        acc << (ai ? "," : "") << "{\"bufferView\":" << bi
            << ",\"componentType\":5125,\"count\":" << v.size() << ",\"type\":\"SCALAR\"}";
        return ai;
    };

    float gmin[3] = {-5, 0, -5}, gmax[3] = {5, 0, 5};
    float emin[3] = {-0.5f, 2, -0.5f}, emax[3] = {0.5f, 2, 0.5f};
    int a_gpP = add_f(gpP, "VEC3", 3, gmin, gmax);
    int a_gpN = add_f(gpN, "VEC3", 3, nullptr, nullptr);
    int a_gpI = add_i(gpI);
    int a_epP = add_f(epP, "VEC3", 3, emin, emax);
    int a_epN = add_f(epN, "VEC3", 3, nullptr, nullptr);
    int a_epI = add_i(epI);

    std::ostringstream mats, meshes, nodes, j;
    mats << "{\"name\":\"ground\",\"pbrMetallicRoughness\":{\"baseColorFactor\":[0.8,0.8,0.8,1],"
            "\"metallicFactor\":0.0,\"roughnessFactor\":1.0}},";
    mats << "{\"name\":\"panel\",\"pbrMetallicRoughness\":{\"baseColorFactor\":[0,0,0,1],"
            "\"metallicFactor\":0.0,\"roughnessFactor\":1.0},"
            "\"emissiveFactor\":[1.0,1.0,1.0],"
            "\"extensions\":{\"KHR_materials_emissive_strength\":{\"emissiveStrength\":5.0}}}";

    auto prim = [&](int pos, int nor, int ind, int mat) {
        std::ostringstream p;
        p << "{\"attributes\":{\"POSITION\":" << pos << ",\"NORMAL\":" << nor
          << "},\"indices\":" << ind << ",\"material\":" << mat << "}";
        return p.str();
    };
    meshes << "{\"primitives\":[" << prim(a_gpP, a_gpN, a_gpI, 0) << "]},";
    meshes << "{\"primitives\":[" << prim(a_epP, a_epN, a_epI, 1) << "]}";

    nodes << "{\"mesh\":0,\"name\":\"ground\"},";
    nodes << "{\"mesh\":1,\"name\":\"panel\"},";
    // Camera at (0,3,6) looking at (0,0.5,0).
    nodes << "{\"name\":\"camera\",\"camera\":0,\"translation\":[0,3,6],"
             "\"rotation\":[-0.1961,0,0,0.9806]}";

    j << "{";
    j << "\"asset\":{\"version\":\"2.0\",\"generator\":\"termrender-emissive-testgen\"},";
    j << "\"extensionsUsed\":[\"KHR_materials_emissive_strength\"],";
    j << "\"scene\":0,";
    j << "\"scenes\":[{\"nodes\":[0,1,2]}],";
    j << "\"nodes\":[" << nodes.str() << "],";
    j << "\"meshes\":[" << meshes.str() << "],";
    j << "\"materials\":[" << mats.str() << "],";
    j << "\"accessors\":[" << acc.str() << "],";
    j << "\"bufferViews\":[" << bview.str() << "],";
    j << "\"buffers\":[{\"byteLength\":" << buf.data.size() << "}],";
    j << "\"cameras\":[{\"type\":\"perspective\",\"perspective\":{\"yfov\":0.9,"
         "\"znear\":0.05,\"zfar\":100,\"aspectRatio\":1.5}}]";
    j << "}";

    std::string jsonStr = j.str();
    while (jsonStr.size() % 4) jsonStr.push_back(' ');

    uint32_t total = (uint32_t)(12 + 8 + jsonStr.size() + 8 + buf.data.size());
    std::ofstream f(outpath, std::ios::binary);
    auto u32 = [&](uint32_t v) { f.write((const char *)&v, 4); };
    u32(0x46546C67);
    u32(2);
    u32(total);
    u32((uint32_t)jsonStr.size());
    u32(0x4E4F534A);
    f.write(jsonStr.data(), jsonStr.size());
    u32((uint32_t)buf.data.size());
    u32(0x004E4942);
    f.write((const char *)buf.data.data(), buf.data.size());
    f.close();

    std::printf("wrote %s: json=%zu bin=%zu total=%u\n", outpath, jsonStr.size(), buf.data.size(),
                total);
    return 0;
}
