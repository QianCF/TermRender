// Dev-only generator for `assets/spheres.glb` (not part of the build).
//
// Scene: checkered ground + a mirror sphere + an emissive sphere, no punctual
// lights, so the emissive sphere is the only light (also exercises emissive NEE).
//
//   g++ -O2 -std=c++17 tools/gen_spheres_glb.cpp -o gen_spheres_glb
//   ./gen_spheres_glb assets/spheres.glb

#include <cmath>
#include <cstdint>
#include <cstdio>
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

int main(int argc, char **argv) {
    const char *outpath = argc > 1 ? argv[1] : "spheres.glb";
    Buf buf;
    std::ostringstream acc, bview;
    int acc_count = 0, bview_count = 0;

    auto add_f = [&](const std::vector<float> &v, const char *type, int comps) -> int {
        size_t off = buf.add(v.data(), v.size() * 4);
        int bi = bview_count++;
        bview << (bi ? "," : "") << "{\"buffer\":0,\"byteOffset\":" << off
              << ",\"byteLength\":" << v.size() * 4 << "}";
        int ai = acc_count++;
        acc << (ai ? "," : "") << "{\"bufferView\":" << bi
            << ",\"componentType\":5126,\"count\":" << (v.size() / comps) << ",\"type\":\"" << type
            << "\"}";
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

    // ---- unit sphere (radius 1, centred at origin), UV sphere ----------
    const int ST = 24, SL = 48;
    std::vector<float> spP, spN, spUV;
    std::vector<uint32_t> spI;
    for (int i = 0; i <= ST; i++) {
        float th = (float)i / ST * (float)M_PI;
        for (int j = 0; j <= SL; j++) {
            float ph = (float)j / SL * 2.0f * (float)M_PI;
            float x = std::sin(th) * std::cos(ph), y = std::cos(th), z = std::sin(th) * std::sin(ph);
            spP.push_back(x);
            spP.push_back(y);
            spP.push_back(z);
            spN.push_back(x);
            spN.push_back(y);
            spN.push_back(z);
            spUV.push_back((float)j / SL);
            spUV.push_back((float)i / ST);
        }
    }
    for (int i = 0; i < ST; i++)
        for (int j = 0; j < SL; j++) {
            uint32_t a = i * (SL + 1) + j, b = a + 1, c = a + SL + 1, d = c + 1;
            spI.insert(spI.end(), {a, c, b, b, c, d});
        }

    // ---- ground plane 20x20 at y=0 -------------------------------------
    std::vector<float> gP = {-10, 0, -10, 10, 0, -10, 10, 0, 10, -10, 0, 10};
    std::vector<float> gN(12, 0.0f);
    for (int i = 0; i < 4; i++) gN[i * 3 + 1] = 1.0f;
    std::vector<float> gUV = {0, 0, 10, 0, 10, 10, 0, 10};
    std::vector<uint32_t> gI = {0, 2, 1, 0, 3, 2};

    // ---- 2x2 checker BMP ------------------------------------------------
    std::vector<uint8_t> bmp;
    {
        int W = 2, H = 2, row = W * 3, ds = row * H, fs = 54 + ds;
        bmp.assign(fs, 0);
        auto p32 = [&](int o, uint32_t v) {
            bmp[o] = v & 0xff; bmp[o + 1] = (v >> 8) & 0xff; bmp[o + 2] = (v >> 16) & 0xff;
            bmp[o + 3] = (v >> 24) & 0xff;
        };
        bmp[0] = 'B'; bmp[1] = 'M'; p32(2, fs); p32(10, 54); p32(14, 40); p32(18, W); p32(22, H);
        bmp[26] = 1; bmp[28] = 24; p32(34, ds);
        for (int y = 0; y < H; y++)
            for (int x = 0; x < W; x++) {
                int o = 54 + y * row + x * 3;
                uint8_t v = ((x + y) & 1) ? 230 : 25;
                bmp[o] = v; bmp[o + 1] = v; bmp[o + 2] = v;
            }
    }

    int a_spP = add_f(spP, "VEC3", 3);
    int a_spN = add_f(spN, "VEC3", 3);
    int a_spUV = add_f(spUV, "VEC2", 2);
    int a_spI = add_i(spI);
    int a_gP = add_f(gP, "VEC3", 3);
    int a_gN = add_f(gN, "VEC3", 3);
    int a_gUV = add_f(gUV, "VEC2", 2);
    int a_gI = add_i(gI);
    int bv_tex = add_raw(bmp);

    std::ostringstream mats, meshes, nodes, j;
    mats << "{\"name\":\"ground\",\"pbrMetallicRoughness\":{\"baseColorTexture\":{\"index\":0},"
            "\"metallicFactor\":0.0,\"roughnessFactor\":0.8}},";
    mats << "{\"name\":\"mirror\",\"pbrMetallicRoughness\":{\"baseColorFactor\":[0.95,0.95,0.95,1],"
            "\"metallicFactor\":1.0,\"roughnessFactor\":0.0}},";
    mats << "{\"name\":\"emissive\",\"pbrMetallicRoughness\":{\"baseColorFactor\":[0,0,0,1],"
            "\"metallicFactor\":0.0,\"roughnessFactor\":1.0},"
            "\"emissiveFactor\":[6.0,5.4,4.6]}";

    auto prim = [&](int pos, int nor, int uv, int ind, int mat) {
        std::ostringstream p;
        p << "{\"attributes\":{\"POSITION\":" << pos << ",\"NORMAL\":" << nor
          << ",\"TEXCOORD_0\":" << uv << "},\"indices\":" << ind << ",\"material\":" << mat << "}";
        return p.str();
    };
    meshes << "{\"primitives\":[" << prim(a_spP, a_spN, a_spUV, a_spI, 1) << "]},";
    meshes << "{\"primitives\":[" << prim(a_spP, a_spN, a_spUV, a_spI, 2) << "]},";
    meshes << "{\"primitives\":[" << prim(a_gP, a_gN, a_gUV, a_gI, 0) << "]}";

    nodes << "{\"mesh\":0,\"name\":\"mirror\",\"translation\":[-1.6,1.0,0]},";
    nodes << "{\"mesh\":1,\"name\":\"emissive\",\"translation\":[1.6,1.0,0]},";
    nodes << "{\"mesh\":2,\"name\":\"ground\"},";
    nodes << "{\"name\":\"camera\",\"camera\":0,\"translation\":[0,3.5,8.0],"
             "\"rotation\":[-0.151,0,0,0.9886]}";

    j << "{";
    j << "\"asset\":{\"version\":\"2.0\",\"generator\":\"termrender-spheres-testgen\"},";
    j << "\"scene\":0,\"scenes\":[{\"nodes\":[0,1,2,3]}],";
    j << "\"nodes\":[" << nodes.str() << "],";
    j << "\"meshes\":[" << meshes.str() << "],";
    j << "\"materials\":[" << mats.str() << "],";
    j << "\"accessors\":[" << acc.str() << "],";
    j << "\"bufferViews\":[" << bview.str() << "],";
    j << "\"buffers\":[{\"byteLength\":" << buf.data.size() << "}],";
    j << "\"samplers\":[{\"wrapS\":10497,\"wrapT\":10497,\"magFilter\":9729,\"minFilter\":9987}],";
    j << "\"images\":[{\"bufferView\":" << bv_tex << ",\"mimeType\":\"image/bmp\"}],";
    j << "\"textures\":[{\"sampler\":0,\"source\":0}],";
    j << "\"cameras\":[{\"type\":\"perspective\",\"perspective\":{\"yfov\":0.8,"
         "\"znear\":0.05,\"zfar\":100,\"aspectRatio\":1.5}}]";
    j << "}";

    std::string jsonStr = j.str();
    while (jsonStr.size() % 4) jsonStr.push_back(' ');
    uint32_t total = (uint32_t)(12 + 8 + jsonStr.size() + 8 + buf.data.size());
    std::ofstream f(outpath, std::ios::binary);
    auto u32 = [&](uint32_t v) { f.write((const char *)&v, 4); };
    u32(0x46546C67); u32(2); u32(total);
    u32((uint32_t)jsonStr.size()); u32(0x4E4F534A);
    f.write(jsonStr.data(), jsonStr.size());
    u32((uint32_t)buf.data.size()); u32(0x004E4942);
    f.write((const char *)buf.data.data(), buf.data.size());
    f.close();
    std::printf("wrote %s: json=%zu bin=%zu total=%u\n", outpath, jsonStr.size(), buf.data.size(),
                total);
    return 0;
}
