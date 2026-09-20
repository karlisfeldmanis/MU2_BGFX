// One placement's transform, built the way MU builds it.
//
// This is its own file, with its own test, because it was got wrong twice: once in the sign
// and once in the order, and both times it looked plausible on most of Lorencia. MU's own
// `AngleMatrix` (MuMain, `Core/Math/ZzzMathLib.cpp:194`) is the Quake one and says what it
// is on the line above the arithmetic:
//
//     // matrix = (Z * Y) * X
//
// So a point is turned by X first, then Y, then Z, in MU's z-up frame. Under the axis swap
// of docs/conventions.md -- MU's (x, y, z) becomes our (x, z, -y) -- MU's z is our yaw, its
// x our pitch and its y our roll, and the order carries over with them: pitch first, then
// roll, then yaw. `bx::mtxSRT` composes them the other way and with the opposite sense, so
// it is not used here at all.
//
// The two are the same matrix whenever one of the angles is zero, which is 2192 of
// Lorencia's 2753 placements, the fountain among them. The 561 that carry both a pitch and
// a yaw are where it shows: 444 of them turn by more than 5 degrees, three `TreasureDrum01`
// by 180.
#pragma once

namespace mu::content {

// `pitch`, `yaw` and `roll` are radians, already MU's own angles about our axes, as the cook
// writes them. `out` is 16 floats, row-vector, translation in row 3 -- what bgfx wants and
// what the instance buffer carries.
void placementTransform(float pitch, float yaw, float roll, float scale, const float position[3],
                        float* out);

}  // namespace mu::content
