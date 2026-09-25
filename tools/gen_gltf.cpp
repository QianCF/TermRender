// Dev-only generator for a small test GLB scene (not part of the build).
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <sstream>
#include <fstream>
#include <cmath>

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
    v.push_back(x); v.push_back(y); v.push_back(z);
}

int main(int argc, char **argv) {
    const char *outpath = argc > 1 ? argv[1] : "test.glb";

    Buf buf;
    std::ostringstream acc, bview, meshes, nodes, mats;
    int acc_count = 0, bview_count = 0;

    // ---- cube (per-face normals) --------------------------------------
    struct Face { float n[3], u[3], v[3]; };
    const Face faces[6] = {
        {{0,0,1},{1,0,0},{0,1,0}},   {{0,0,-1},{-1,0,0},{0,1,0}},
        {{1,0,0},{0,0,-1},{0,1,0}},  {{-1,0,0},{0,0,1},{0,1,0}},
        {{0,1,0},{1,0,0},{0,0,-1}},  {{0,-1,0},{1,0,0},{0,0,1}},
    };
    std::vector<float> cubeP, cubeN;
    std::vector<uint32_t> cubeI;
    for (int f = 0; f < 6; f++) {
        const Face &fc = faces[f];
        float cx = fc.n[0] * 0.5f, cy = 0.5f + fc.n[1] * 0.5f, cz = fc.n[2] * 0.5f;
        uint32_t base = (uint32_t)(cubeP.size() / 3);
        const float su[4] = {-1, 1, 1, -1};
        const float sv[4] = {-1, -1, 1, 1};
        for (int c = 0; c < 4; c++) {
            push3(cubeP,
                  cx + fc.u[0] * su[c] * 0.5f + fc.v[0] * sv[c] * 0.5f,
                  cy + fc.u[1] * su[c] * 0.5f + fc.v[1] * sv[c] * 0.5f,
                  cz + fc.u[2] * su[c] * 0.5f + fc.v[2] * sv[c] * 0.5f);
            push3(cubeN, fc.n[0], fc.n[1], fc.n[2]);
        }
        cubeI.insert(cubeI.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
    }

    // ---- ground plane --------------------------------------------------
    std::vector<float> gpP = {-5,0,-5, 5,0,-5, 5,0,5, -5,0,5};
    std::vector<float> gpN(12, 0.0f);
    for (int i = 0; i < 4; i++) gpN[i * 3 + 1] = 1.0f;
    std::vector<float> gpUV = {0, 0, 4, 0, 4, 4, 0, 4};
    std::vector<uint32_t> gpI = {0, 2, 1, 0, 3, 2};

    // 4x4 checker BMP (red / white) to exercise texture decoding + sampling.
    std::vector<uint8_t> bmp;
    {
        int W = 4, H = 4, rowbytes = W * 3, datasize = rowbytes * H, filesize = 54 + datasize;
        bmp.assign(filesize, 0);
        auto put32 = [&](int off, uint32_t v) {
            bmp[off] = v & 0xff; bmp[off + 1] = (v >> 8) & 0xff;
            bmp[off + 2] = (v >> 16) & 0xff; bmp[off + 3] = (v >> 24) & 0xff;
        };
        bmp[0] = 'B'; bmp[1] = 'M';
        put32(2, filesize); put32(10, 54);
        put32(14, 40); put32(18, W); put32(22, H);
        bmp[26] = 1; bmp[28] = 24; put32(34, datasize);
        for (int y = 0; y < H; y++)
            for (int x = 0; x < W; x++) {
                int off = 54 + y * rowbytes + x * 3;
                bool on = (((x / 2) + (y / 2)) & 1) != 0;
                uint8_t R = 255, G = on ? 0 : 255, B = on ? 0 : 255;
                bmp[off] = B; bmp[off + 1] = G; bmp[off + 2] = R;
            }
    }

    // ---- emissive panel (facing -Z toward the scene) ------------------
    std::vector<float> epP = {-0.5f, 1.3f, 0.05f, 0.5f, 1.3f, 0.05f,
                              0.5f, 2.3f, 0.05f, -0.5f, 2.3f, 0.05f};
    std::vector<float> epN(12, 0.0f);
    for (int i = 0; i < 4; i++) epN[i * 3 + 2] = 1.0f;
    std::vector<uint32_t> epI = {0, 1, 2, 0, 2, 3};

    // Transparent panel (alphaMode BLEND) between the camera and the scene.
    std::vector<float> tpP = {-3.0f, 0.0f, 2.0f, 3.0f, 0.0f, 2.0f,
                              3.0f, 4.0f, 2.0f, -3.0f, 4.0f, 2.0f};
    std::vector<float> tpN(12, 0.0f);
    for (int i = 0; i < 4; i++) tpN[i * 3 + 2] = 1.0f;
    std::vector<uint32_t> tpI = {0, 1, 2, 0, 2, 3};

    auto add_f = [&](const std::vector<float> &v, const char *type, int comps,
                     const float *mn, const float *mx) -> int {
        size_t off = buf.add(v.data(), v.size() * 4);
        int bi = bview_count++;
        bview << (bi ? "," : "") << "{\"buffer\":0,\"byteOffset\":" << off
              << ",\"byteLength\":" << v.size() * 4 << "}";
        int ai = acc_count++;
        acc << (ai ? "," : "") << "{\"bufferView\":" << bi
            << ",\"componentType\":5126,\"count\":" << (v.size() / comps)
            << ",\"type\":\"" << type << "\"";
        if (mn && mx)
            acc << ",\"min\":[" << mn[0] << "," << mn[1] << "," << mn[2] << "]"
                << ",\"max\":[" << mx[0] << "," << mx[1] << "," << mx[2] << "]";
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
    auto add_raw = [&](const std::vector<uint8_t> &v) -> int {
        size_t off = buf.add(v.data(), v.size());
        int bi = bview_count++;
        bview << (bi ? "," : "") << "{\"buffer\":0,\"byteOffset\":" << off
              << ",\"byteLength\":" << v.size() << "}";
        return bi;
    };

    float cmin[3] = {-0.5f, 0.0f, -0.5f}, cmax[3] = {0.5f, 1.0f, 0.5f};
    float gmin[3] = {-5, 0, -5}, gmax[3] = {5, 0, 5};
    float emin[3] = {-0.5f, 1.3f, 0.05f}, emax[3] = {0.5f, 2.3f, 0.05f};
    int a_cubeP = add_f(cubeP, "VEC3", 3, cmin, cmax);
    int a_cubeN = add_f(cubeN, "VEC3", 3, nullptr, nullptr);
    int a_cubeI = add_i(cubeI);
    int a_gpP = add_f(gpP, "VEC3", 3, gmin, gmax);
    int a_gpN = add_f(gpN, "VEC3", 3, nullptr, nullptr);
    int a_gpUV = add_f(gpUV, "VEC2", 2, nullptr, nullptr);
    int a_gpI = add_i(gpI);
    int a_epP = add_f(epP, "VEC3", 3, emin, emax);
    int a_epN = add_f(epN, "VEC3", 3, nullptr, nullptr);
    int a_epI = add_i(epI);
    float tmin3[3] = {-3, 0, 2}, tmax3[3] = {3, 4, 2};
    int a_tpP = add_f(tpP, "VEC3", 3, tmin3, tmax3);
    int a_tpN = add_f(tpN, "VEC3", 3, nullptr, nullptr);
    int a_tpI = add_i(tpI);
    int bv_tex = add_raw(bmp);

    mats << "{\"name\":\"red\",\"pbrMetallicRoughness\":{\"baseColorFactor\":[0.8,0.15,0.12,1],"
            "\"metallicFactor\":0.0,\"roughnessFactor\":0.35}},";
    mats << "{\"name\":\"ground\",\"pbrMetallicRoughness\":{\"baseColorFactor\":[0.9,0.9,0.9,1],"
            "\"metallicFactor\":0.0,\"roughnessFactor\":0.9,\"baseColorTexture\":{\"index\":0}}},";
    mats << "{\"name\":\"lamp\",\"pbrMetallicRoughness\":{\"baseColorFactor\":[0,0,0,1],"
            "\"metallicFactor\":0,\"roughnessFactor\":1},"
            "\"emissiveFactor\":[1.0,0.85,0.6],"
            "\"extensions\":{\"KHR_materials_emissive_strength\":{\"emissiveStrength\":8.0}}},";
    mats << "{\"name\":\"glass\",\"pbrMetallicRoughness\":{\"baseColorFactor\":[0.2,0.45,1.0,0.5],"
            "\"metallicFactor\":0.0,\"roughnessFactor\":0.15},\"alphaMode\":\"BLEND\","
            "\"doubleSided\":true}";

    auto prim = [&](int pos, int nor, int ind, int mat) {
        std::ostringstream p;
        p << "{\"attributes\":{\"POSITION\":" << pos << ",\"NORMAL\":" << nor
          << "},\"indices\":" << ind << ",\"material\":" << mat << "}";
        return p.str();
    };
    meshes << "{\"primitives\":[" << prim(a_cubeP, a_cubeN, a_cubeI, 0) << "]},";
    meshes << "{\"primitives\":[{\"attributes\":{\"POSITION\":" << a_gpP << ",\"NORMAL\":" << a_gpN
           << ",\"TEXCOORD_0\":" << a_gpUV << "},\"indices\":" << a_gpI << ",\"material\":1}]},";
    meshes << "{\"primitives\":[" << prim(a_epP, a_epN, a_epI, 2) << "]},";
    meshes << "{\"primitives\":[" << prim(a_tpP, a_tpN, a_tpI, 3) << "]}";

    nodes << "{\"mesh\":0,\"name\":\"cube\"},";
    nodes << "{\"mesh\":1,\"name\":\"ground\"},";
    nodes << "{\"mesh\":2,\"name\":\"lamp\"},";
    nodes << "{\"mesh\":3,\"name\":\"glass\"},";
    nodes << "{\"name\":\"sun\",\"extensions\":{\"KHR_lights_punctual\":{\"light\":0}},"
             "\"translation\":[2,6,3],\"rotation\":[-0.4,0.2,0.1,0.88]},";
    nodes << "{\"name\":\"bulb\",\"extensions\":{\"KHR_lights_punctual\":{\"light\":1}},"
             "\"translation\":[-1.6,2.2,1.2]},";
    nodes << "{\"name\":\"camera\",\"camera\":0,\"translation\":[3.2,2.0,4.2],"
             "\"rotation\":[-0.18,0.36,0,0.915]}";

    std::ostringstream j;
    j << "{";
    j << "\"asset\":{\"version\":\"2.0\",\"generator\":\"termrender-testgen\"},";
    j << "\"extensionsUsed\":[\"KHR_lights_punctual\",\"KHR_materials_emissive_strength\"],";
    j << "\"scene\":0,";
    j << "\"scenes\":[{\"nodes\":[0,1,2,3,4,5,6]}],";
    j << "\"nodes\":[" << nodes.str() << "],";
    j << "\"meshes\":[" << meshes.str() << "],";
    j << "\"materials\":[" << mats.str() << "],";
    j << "\"accessors\":[" << acc.str() << "],";
    j << "\"bufferViews\":[" << bview.str() << "],";
    j << "\"buffers\":[{\"byteLength\":" << buf.data.size() << "}],";
    j << "\"samplers\":[{\"wrapS\":10497,\"wrapT\":10497,\"magFilter\":9729,\"minFilter\":9987}],";
    j << "\"images\":[{\"bufferView\":" << bv_tex << ",\"mimeType\":\"image/bmp\"}],";
    j << "\"textures\":[{\"sampler\":0,\"source\":0}],";
    j << "\"cameras\":[{\"type\":\"perspective\",\"perspective\":{\"yfov\":0.7,"
         "\"znear\":0.05,\"zfar\":100,\"aspectRatio\":1.5}}],";
    j << "\"extensions\":{\"KHR_lights_punctual\":{\"lights\":["
         "{\"type\":\"directional\",\"color\":[1.0,0.95,0.85],\"intensity\":3.0},"
         "{\"type\":\"point\",\"color\":[1.0,0.9,0.8],\"intensity\":25.0}]}}";
    j << "}";

    std::string jsonStr = j.str();
    while (jsonStr.size() % 4) jsonStr.push_back(' ');

    bool gltfMode = false;
    {
        std::string o = outpath;
        if (o.size() >= 5 && o.compare(o.size() - 5, 5, ".gltf") == 0) gltfMode = true;
    }

    if (gltfMode) {
        static const char *b64c =
            "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        std::string b64;
        const std::vector<uint8_t> &d = buf.data;
        for (size_t i = 0; i < d.size(); i += 3) {
            uint32_t v = (uint32_t)d[i] << 16;
            if (i + 1 < d.size()) v |= (uint32_t)d[i + 1] << 8;
            if (i + 2 < d.size()) v |= d[i + 2];
            b64.push_back(b64c[(v >> 18) & 63]);
            b64.push_back(b64c[(v >> 12) & 63]);
            b64.push_back(i + 1 < d.size() ? b64c[(v >> 6) & 63] : '=');
            b64.push_back(i + 2 < d.size() ? b64c[v & 63] : '=');
        }
        std::string plain = "\"buffers\":[{\"byteLength\":" + std::to_string(buf.data.size()) + "}]";
        std::string uri = "\"buffers\":[{\"byteLength\":" + std::to_string(buf.data.size()) +
                          ",\"uri\":\"data:application/octet-stream;base64," + b64 + "\"}]";
        size_t pos = jsonStr.find(plain);
        if (pos != std::string::npos) jsonStr.replace(pos, plain.size(), uri);
        while (jsonStr.size() % 4) jsonStr.push_back(' ');

        std::ofstream f(outpath, std::ios::binary);
        f.write(jsonStr.data(), jsonStr.size());
        f.close();
        std::printf("wrote %s (gltf, base64 buffer): json=%zu bin=%zu\n", outpath,
                    jsonStr.size(), buf.data.size());
        return 0;
    }

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

    std::printf("wrote %s: json=%zu bin=%zu total=%u\n", outpath, jsonStr.size(),
                buf.data.size(), total);
    return 0;
}
