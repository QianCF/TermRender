#pragma once
// Public surface of the translated slang-pbr kernel (see pbr_kernel.cpp).
// Struct layout mirrors the "stable" API of dspbr-pt's material kernel.
#include "math3d.h"
#include "pbr_prelude.h"
#include <cstdint>

namespace tr {
namespace pbr {

struct PbrGltfMaterial {
    Vec4 baseColorFactor;
    float metallicFactor;
    float roughnessFactor;
    Vec3 emissiveFactor;
    float emissiveStrength;
    float specularFactor;
    Vec3 specularColorFactor;
    float transmissionFactor;
    float diffuseTransmissionFactor;
    Vec3 diffuseTransmissionColorFactor;
    float ior;
    Vec3 attenuationColor;
    float attenuationDistance;
    float thicknessFactor;
    Vec3 multiscatterColorFactor;
    float scatterAnisotropy;
    float clearcoatFactor;
    float clearcoatRoughnessFactor;
    float clearcoatNormalTextureScale;
    Vec3 sheenColorFactor;
    float sheenRoughnessFactor;
    float anisotropyStrength;
    float anisotropyRotation;
    float iridescenceFactor;
    float iridescenceIor;
    float iridescenceThickness;
    float dispersion;
    float normalTextureScale;
    uint32_t featureMask;

    static PbrGltfMaterial defaults();
};

struct PbrLayerNormals {
    Vec3 rawGeometry;
    Vec3 geometry;
    Vec3 shadingGeometry;
    Vec3 interfaceBase;
    Vec3 base;
    Vec3 clearcoat;
    Vec3 anisotropyTangent;
};

struct PbrClosure {
    Vec3 diffuseColor;
    Vec3 diffuseReflectionColor;
    Vec3 transmissionColor;
    float diffuseWeight;
    float sheenWeight;
    float anisotropy;
    Vec3 anisotropyTangent;
    Vec3 specularColor;
    Vec3 specularF90;
    Vec3 dielectricSpecularColor;
    Vec3 dielectricSpecularF90;
    float dielectricWeight;
    float specularWeight;
    float clearcoatWeight;
    float roughness;
    Vec3 sheenColor;
    float sheenRoughness;
    float transparency;
    float clearcoat;
    float clearcoatRoughness;
    Vec3 throughput;
    float metallic;
    float specular;
    float ior;
    float iridescence;
    float iridescenceIor;
    float iridescenceThickness;
    float dispersion;
    float thinWalled;
    Vec3 attenuationColor;
    float attenuationDistance;
    Vec3 multiscatterColor;
    float scatterAnisotropy;
    float transmissionWeight;
    Vec3 diffuseTransmissionColor;
    float diffuseTransmissionWeight;
};

struct PbrTransport {
    float currentMediumIor;
    float interfaceIor;
    float thinWalled;
    float _pad0;
};

struct PbrDirections {
    Vec3 viewDir;
    Vec3 lightDir;
};

struct PbrNormals {
    Vec3 rawGeometryNormal;
    Vec3 transmissionNormal;
    Vec3 baseNormal;
    Vec3 clearcoatNormal;
};

struct PbrRandoms {
    float component;
    Vec2 lobe;
    float boundary;
};

struct PbrSample {
    Vec3 direction;
    float pdf;
    Vec3 bsdfOverPdf;
    float specular;
    float crossedBoundary;
    float nextMediumIor;
};

struct PbrGltfState {
    PbrGltfMaterial material;
    PbrClosure closure;
    float currentMediumIor;
};

struct PbrMaterial {
    Vec3 albedo;
    float metallic;
    float roughness;
    float anisotropy;
    Vec3 anisotropyDirection;
    float transparency;
    float ior;
    Vec3 specularColor;
    float specular;
    Vec3 emission;
    float normalScale;
    Vec3 attenuationColor;
    float attenuationDistance;
    Vec3 multiscatterColor;
    float scatterAnisotropy;
    float thinWalled;
    float translucency;
    Vec3 translucencyColor;
    float iridescence;
    float iridescenceIor;
    float iridescenceThickness;
    float dispersion;
    float clearcoat;
    float clearcoatRoughness;
    Vec3 sheenColor;
    float sheenRoughness;
    float clearcoatNormalScale;
    float frontFaceEmissionOnly;
};

struct PbrVolume {
    Vec3 sigmaT;
    float isActive;
    Vec3 rhoSs;
    float scatterAnisotropy;
    Vec3 sigmaA;
    float _pad0;
};

struct PbrPhaseSample {
    Vec3 direction;
    float pdf;
};

struct PbrState {
    PbrMaterial surface;
    PbrClosure closure;
    float currentMediumIor;
};

// Hand-written helper (pbr_api.cpp): a default-initialized glTF material.
PbrGltfMaterial defaultGltfPbrMaterial();

PbrClosure pbrBuildClosureFromGltf(PbrGltfMaterial material, PbrLayerNormals layerNormals,
                                   Vec3 anisotropyTangent);
PbrGltfState pbrPrepareStateFromGltf(PbrGltfMaterial material, PbrClosure closure,
                                     PbrTransport transport);
Vec3 pbrEvalGltfState(PbrGltfState state, PbrDirections directions, PbrNormals normals);
float pbrPdfGltfState(PbrGltfState state, PbrDirections directions, PbrNormals normals);
PbrSample pbrSampleGltfState(PbrGltfState state, PbrDirections directions, PbrNormals normals,
                             PbrRandoms randoms);

// event type flags (dspbr-pt)
enum { E_DELTA = 0x2, E_REFLECTION = 0x4, E_TRANSMISSION = 0x8 };

} // namespace pbr
} // namespace tr
