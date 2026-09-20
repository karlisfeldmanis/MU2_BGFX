#include "content/placement.h"

#include <cmath>
#include <cstring>

namespace mu::content {
namespace {

// A rotation about our +y, right-handed, written for a row vector: v * M. Yaw turns +x
// toward -z, which is what MU's z-rotation does once its axes are swapped into ours.
//
// These are written out rather than taken from bx, whose mtxRotate* and mtxSRT turn the
// other way -- the same trap docs/conventions.md records for mtxFromQuaternion. Negating
// every angle on the way into bx worked and read as a correction of MU rather than of bx,
// which is how the *order* went unnoticed behind it.
void rotateY(float angle, float* m) {
    const float s = std::sin(angle);
    const float c = std::cos(angle);
    m[0] = c;    m[1] = 0.0f; m[2] = -s;
    m[3] = 0.0f; m[4] = 1.0f; m[5] = 0.0f;
    m[6] = s;    m[7] = 0.0f; m[8] = c;
}

// About our +x: +y toward +z.
void rotateX(float angle, float* m) {
    const float s = std::sin(angle);
    const float c = std::cos(angle);
    m[0] = 1.0f; m[1] = 0.0f; m[2] = 0.0f;
    m[3] = 0.0f; m[4] = c;    m[5] = s;
    m[6] = 0.0f; m[7] = -s;   m[8] = c;
}

// About our +z: +x toward +y.
void rotateZ(float angle, float* m) {
    const float s = std::sin(angle);
    const float c = std::cos(angle);
    m[0] = c;    m[1] = s;    m[2] = 0.0f;
    m[3] = -s;   m[4] = c;    m[5] = 0.0f;
    m[6] = 0.0f; m[7] = 0.0f; m[8] = 1.0f;
}

// c = a * b, three by three, row-vector: a is applied first.
void mul3(const float* a, const float* b, float* c) {
    for (int row = 0; row < 3; ++row) {
        for (int column = 0; column < 3; ++column) {
            c[row * 3 + column] = a[row * 3 + 0] * b[0 * 3 + column] +
                                  a[row * 3 + 1] * b[1 * 3 + column] +
                                  a[row * 3 + 2] * b[2 * 3 + column];
        }
    }
}

}  // namespace

void placementTransform(float pitch, float yaw, float roll, float scale, const float position[3],
                        float* out) {
    float x[9], z[9], y[9], xz[9], rotation[9];
    rotateX(pitch, x);
    rotateZ(roll, z);
    rotateY(yaw, y);
    // MU's (Z * Y) * X, in our axes and for a row vector: pitch, then roll, then yaw.
    mul3(x, z, xz);
    mul3(xz, y, rotation);

    std::memset(out, 0, sizeof(float) * 16);
    for (int row = 0; row < 3; ++row) {
        for (int column = 0; column < 3; ++column) {
            out[row * 4 + column] = rotation[row * 3 + column] * scale;
        }
    }
    out[12] = position[0];
    out[13] = position[1];
    out[14] = position[2];
    out[15] = 1.0f;
}

}  // namespace mu::content
