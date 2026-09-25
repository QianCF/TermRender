$ErrorActionPreference = "Stop"
$src = "C:\Users\qianyu\AppData\Local\Temp\opencode\dspbr\material_kernel.glsl"
$dst = "C:\Users\qianyu\Downloads\TermRender\src\pbr_kernel.cpp"
$text = Get-Content $src -Raw

# --- cut: keep everything before the volume helpers --------------------------
$cut = $text.IndexOf("PbrVolume pbrClearVolume()")
if ($cut -lt 0) { throw "cut marker not found" }
$text = $text.Substring(0, $cut)

# --- drop the stable struct block (pbr.h provides it) -------------------------
$s0 = $text.IndexOf("struct PbrGltfMaterial")
$s1 = $text.IndexOf("GltfPbrMaterial_0 pbrToGeneratedGltfPbrMaterial")
if ($s0 -ge 0 -and $s1 -gt $s0) { $text = $text.Substring(0, $s0) + $text.Substring($s1) }

# --- numeric literals -> float ----------------------------------------------
$text = [regex]::Replace($text, '(?<![\w\.])(\d+\.\d+(?:[eE][+-]?\d+)?)(?![f\w])', '$1f')
$text = [regex]::Replace($text, '(?<![\w\.])(\.\d+(?:[eE][+-]?\d+)?)(?![f\w])', '0$1f')

# --- types -------------------------------------------------------------------
$text = $text -replace '\bvec2\b', 'Vec2' -replace '\bvec3\b', 'Vec3' -replace '\bvec4\b', 'Vec4'
$text = $text -replace '\buint\b', 'uint32_t'
$text = $text -replace '\bbool\b', 'bool'

# --- parameter qualifiers ------------------------------------------------------
$text = [regex]::Replace($text, '\bconst in ', '')
$text = [regex]::Replace($text, '\binout ', '')
$text = [regex]::Replace($text, '\bout (uint32_t|bool|float|int) ([A-Za-z_0-9]+)', '$1& $2')
$text = [regex]::Replace($text, '\bout (Vec2|Vec3|Vec4) ([A-Za-z_0-9]+)', '$1& $2')

# --- scalar builtins -----------------------------------------------------------
$text = $text -replace '\bpow\(', 'powf_('
$text = $text -replace '\bacos\(', 'std::acos('
$text = $text -replace '\basin\(', 'std::asin('
$text = $text -replace '\batan\(', 'std::atan('
$text = $text -replace '\bsaturate\(', 'saturate_('
# restore vector math calls that must NOT become std::/scalar helpers
$text = $text -replace 'std::acos\(dot\(', 'ACOSDOT('   # protect common pattern? (none expected) -- no-op safeguard

# scalar min/max/abs/exp/log/sqrt stay via overloads in prelude; pow handled above.
# float comparisons fine.

# --- swizzles -------------------------------------------------------------------
$text = $text -replace '\.xyz\b', '.xyz()' -replace '\.rgb\b', '.rgb()' -replace '\.xy\b(?!s)', '.xy()'

# --- featureMask unused uint ops ----------------------------------------------
$text = $text -replace '0U', '0u'

$header = @'
// AUTO-TRANSLATED from dspbr-pt's generated slang-pbr material kernel
// (packages/lib/shader/generated/slang_materials/webgl-full/material_kernel.glsl)
// to C++ by tools/convert_pbr.ps1. Do not edit by hand.
#include "pbr.h"

namespace tr {
namespace pbr {

'@
$footer = @'

// ---- hand-written wrapper: default glTF material ---------------------------
PbrGltfMaterial defaultGltfPbrMaterial()
{
    GltfPbrMaterial_0 m = defaultGltfPbrMaterial_0();
    PbrGltfMaterial o;
    o.baseColorFactor = m.baseColorFactor_0;
    o.metallicFactor = m.metallicFactor_0;
    o.roughnessFactor = m.roughnessFactor_0;
    o.emissiveFactor = m.emissiveFactor_0;
    o.emissiveStrength = m.emissiveStrength_0;
    o.specularFactor = m.specularFactor_0;
    o.specularColorFactor = m.specularColorFactor_0;
    o.transmissionFactor = m.transmissionFactor_0;
    o.diffuseTransmissionFactor = m.diffuseTransmissionFactor_0;
    o.diffuseTransmissionColorFactor = m.diffuseTransmissionColorFactor_0;
    o.ior = m.ior_0;
    o.attenuationColor = m.attenuationColor_0;
    o.attenuationDistance = m.attenuationDistance_0;
    o.thicknessFactor = m.thicknessFactor_0;
    o.multiscatterColorFactor = m.multiscatterColorFactor_0;
    o.scatterAnisotropy = m.scatterAnisotropy_0;
    o.clearcoatFactor = m.clearcoatFactor_0;
    o.clearcoatRoughnessFactor = m.clearcoatRoughnessFactor_0;
    o.clearcoatNormalTextureScale = m.clearcoatNormalTextureScale_0;
    o.sheenColorFactor = m.sheenColorFactor_0;
    o.sheenRoughnessFactor = m.sheenRoughnessFactor_0;
    o.anisotropyStrength = m.anisotropyStrength_0;
    o.anisotropyRotation = m.anisotropyRotation_0;
    o.iridescenceFactor = m.iridescenceFactor_0;
    o.iridescenceIor = m.iridescenceIor_0;
    o.iridescenceThickness = m.iridescenceThickness_0;
    o.dispersion = m.dispersion_0;
    o.normalTextureScale = m.normalTextureScale_0;
    o.featureMask = m.featureMask_0;
    return o;
}

} // namespace pbr
} // namespace tr
'@
$out = $header + $text + $footer
Set-Content -Path $dst -Value $out -NoNewline -Encoding ascii
"written: $((Get-Item $dst).Length) bytes"
