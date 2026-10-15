// Direct3D 11's compiler, fxc, for the DXBC shaders only (docs/plans/low-end-windows.md, P7).
// Metal and SPIR-V never see any of this. Included by common.sh, which every shader reads.
#ifndef MU2_FXC_SH
#define MU2_FXC_SH

#if BGFX_SHADER_LANGUAGE_HLSL

// Warnings fxc gives that change nothing on any GPU, silenced because the build treats warnings
// as errors: a for loop's `int i` declared again in the same function (fxc's old scoping; every
// loop sets its own to 0), pow() of what may be negative (NaN on every API alike), an integer
// divide, a texture read inside a loop it then unrolls, and a value "potentially uninitialized"
// that every branch returns.
#pragma warning(disable : 3078)
#pragma warning(disable : 3571)
#pragma warning(disable : 3556)
#pragma warning(disable : 3570)
#pragma warning(disable : 4000)

// isnan() by its bits. fxc compiles without IEEE strictness and folds isnan() to false ("value
// cannot be NaN", X3577), which would take every NaN guard out of the picture -- the bloom's, the
// present's, the probe's. A NaN is all exponent bits set and a mantissa that is not zero.
bool muIsNan(float x) { return (asuint(x) & 0x7fffffffu) > 0x7f800000u; }
bool3 muIsNan(float3 x) { return (asuint(x) & 0x7fffffffu) > 0x7f800000u; }
#define isnan muIsNan

#endif  // BGFX_SHADER_LANGUAGE_HLSL

#endif  // MU2_FXC_SH
