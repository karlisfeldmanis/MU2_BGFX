// The small maths the engine owns rather than borrows: quaternions, and what a pose does
// with them.
//
// This file exists because of one line in docs/conventions.md: `bx::mtxFromQuaternion`
// writes a COLUMN-vector matrix into bx's row-vector layout, which is the inverse rotation,
// and every figure in the project is about to be built out of sixty of them. A rotation that
// is inverted looks like a bad export rather than like a bug, so there is exactly one
// quaternion-to-matrix in this project and it is here.
#pragma once

#include <cmath>
#include <cstring>

namespace mu::core {

// A rotation as a row-vector matrix: `v * M`, the layout bx and bgfx use, translation in
// row 3. This is the transpose of the column-vector form most references print, which is
// precisely the difference bx gets wrong.
inline void quatToMatrix(const float* q, float* out) {
    const float x = q[0], y = q[1], z = q[2], w = q[3];
    out[0] = 1.0f - 2.0f * (y * y + z * z);
    out[1] = 2.0f * (x * y + z * w);
    out[2] = 2.0f * (x * z - y * w);
    out[3] = 0.0f;
    out[4] = 2.0f * (x * y - z * w);
    out[5] = 1.0f - 2.0f * (x * x + z * z);
    out[6] = 2.0f * (y * z + x * w);
    out[7] = 0.0f;
    out[8] = 2.0f * (x * z + y * w);
    out[9] = 2.0f * (y * z - x * w);
    out[10] = 1.0f - 2.0f * (x * x + y * y);
    out[11] = 0.0f;
    out[12] = 0.0f;
    out[13] = 0.0f;
    out[14] = 0.0f;
    out[15] = 1.0f;
}

// A rotation and a translation into one row-vector matrix. No scale: nothing in MU2's
// figure content animates one, and the cook refuses a clip that does.
inline void composeMatrix(const float* rotation, const float* translation, float* out) {
    quatToMatrix(rotation, out);
    out[12] = translation[0];
    out[13] = translation[1];
    out[14] = translation[2];
}

// Normalised linear interpolation, taking the short way round. Two quaternions a rotation
// apart of more than 180 degrees interpolate the long way unless one is negated first, and
// over a crossfade that reads as a limb swinging the wrong way round the body. Nlerp rather
// than slerp because the angles a crossfade covers are small and the difference is under a
// degree where it is not; MU2 blends the same way.
inline void nlerpQuat(const float* a, const float* b, float t, float* out) {
    float dot = a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3];
    const float sign = dot < 0.0f ? -1.0f : 1.0f;
    float length = 0.0f;
    for (int i = 0; i < 4; ++i) {
        out[i] = a[i] * (1.0f - t) + b[i] * sign * t;
        length += out[i] * out[i];
    }
    length = std::sqrt(length);
    if (length < 1e-8f) {
        out[0] = out[1] = out[2] = 0.0f;
        out[3] = 1.0f;
        return;
    }
    for (int i = 0; i < 4; ++i) out[i] /= length;
}

inline void lerpVec3(const float* a, const float* b, float t, float* out) {
    for (int i = 0; i < 3; ++i) out[i] = a[i] * (1.0f - t) + b[i] * t;
}

// Row-vector multiply: `out = a * b`, which turns by `a` first. Same as bx::mtxMul, written
// here so that core/maths does not need bx and so a pose can be tested without a window.
inline void mulMatrix(const float* a, const float* b, float* out) {
    for (int row = 0; row < 4; ++row) {
        for (int column = 0; column < 4; ++column) {
            float sum = 0.0f;
            for (int k = 0; k < 4; ++k) sum += a[row * 4 + k] * b[k * 4 + column];
            out[row * 4 + column] = sum;
        }
    }
}

// The three rows a bone occupies in the palette texture: the TRANSPOSE of the row-vector
// matrix, because the shaders read an instance's matrix as columns and multiply on the left
// (`mul(m, vec4(position, 1.0))`), which is the column-vector convention. Writing the rows
// straight through instead transposes the rotation -- another inverse rotation, and the same
// afternoon paid for twice.
inline void writePaletteRows(const float* matrix, float* out12) {
    for (int column = 0; column < 3; ++column) {
        for (int row = 0; row < 4; ++row) {
            out12[column * 4 + row] = matrix[row * 4 + column];
        }
    }
}

}  // namespace mu::core
