// Our placement transform against MU's own, for angles MU actually stores.
//
// The reference here is `AngleMatrix` from MuMain's `Core/Math/ZzzMathLib.cpp:194`, copied
// in verbatim below, and the test is the only honest way to know our matrix agrees with it:
// a point is turned by MU's matrix in MU's z-up frame and then swapped into ours, and turned
// by ours in our frame, and the two must land in the same place.
//
// This exists because the transform was wrong twice and looked right both times. First the
// sign -- every placement turned the wrong way, invisible on fences, grass and square
// planters, caught by eye on the fountain. Then the order: `bx::mtxSRT` composes X·Y·Z where
// MU composes (Z·Y)·X, which agrees exactly whenever one angle is zero, and 2192 of
// Lorencia's 2753 placements have one. The 561 that do not were wrong, 444 of them by more
// than five degrees.
//
//     cmake --build build --target placement_test && build/placement_test

#include "content/placement.h"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace {

int g_failures = 0;

// MuMain, Core/Math/ZzzMathLib.cpp:194, unchanged but for its types. angles are degrees,
// (x, y, z) = MU's own roll, pitch, yaw about its z-up axes; the comment is theirs.
void angleMatrix(const float angles[3], float matrix[3][4]) {
    float angle, sr, sp, sy, cr, cp, cy;
    const float toRadians = 3.14159265358979323846f * 2.0f / 360.0f;
    angle = angles[2] * toRadians;
    sy = std::sin(angle);
    cy = std::cos(angle);
    angle = angles[1] * toRadians;
    sp = std::sin(angle);
    cp = std::cos(angle);
    angle = angles[0] * toRadians;
    sr = std::sin(angle);
    cr = std::cos(angle);

    // matrix = (Z * Y) * X
    matrix[0][0] = cp * cy;
    matrix[1][0] = cp * sy;
    matrix[2][0] = -sp;
    matrix[0][1] = sr * sp * cy + cr * -sy;
    matrix[1][1] = sr * sp * sy + cr * cy;
    matrix[2][1] = sr * cp;
    matrix[0][2] = (cr * sp * cy + -sr * -sy);
    matrix[1][2] = (cr * sp * sy + -sr * cy);
    matrix[2][2] = cr * cp;
    matrix[0][3] = 0.0f;
    matrix[1][3] = 0.0f;
    matrix[2][3] = 0.0f;
}

// MU turns a point by that matrix as a column vector: out[i] = sum_j matrix[i][j] * in[j].
void turnLikeMu(const float angles[3], const float in[3], float out[3]) {
    float m[3][4];
    angleMatrix(angles, m);
    for (int i = 0; i < 3; ++i) {
        out[i] = m[i][0] * in[0] + m[i][1] * in[1] + m[i][2] * in[2];
    }
}

// docs/conventions.md: MU's (x, y, z) is our (x, z, -y).
void toOurs(const float mu[3], float out[3]) {
    out[0] = mu[0];
    out[1] = mu[2];
    out[2] = -mu[1];
}

void check(bool condition, const std::string& what) {
    if (!condition) {
        std::printf("  FAIL %s\n", what.c_str());
        ++g_failures;
    }
}

}  // namespace

int main() {
    const float degrees = 3.14159265358979323846f / 180.0f;

    // Angles MU actually stores in Lorencia, plus a few that exercise every axis at once.
    const float cases[][3] = {
        {0, 0, 0},      {0, 0, 90},     {0, 0, 180},    {0, 0, 2250},  {0, 0, -270},
        {5, 0, 0},      {-5, 0, 0},     {30, 0, 45},    {-20, 0, 135}, {110, 0, 120},
        {-380, 0, 365}, {10, 0, 1080},  {45, 30, 60},   {-45, -30, -60},
    };
    const float points[][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}, {0.3f, -0.7f, 0.5f}, {2, 3, -4}};

    float worstError = 0.0f;
    for (const auto& angle : cases) {
        // Ours, as the cook writes it and game/town.cpp reads it: MU's x is our pitch and
        // its z our yaw, both at the same angle, because those axes map to ours directly.
        // Its y maps to our NEGATIVE z, so the roll is negated -- the axis swap is a
        // rotation about x by -90 degrees, and conjugating by it sends MU's +y to our -z.
        // No placement in Lorencia or Noria carries a y angle (0 of 2845 and 0 of 9405), so
        // nothing in either map depends on this; it is here so that a map which does cannot
        // find it out the hard way.
        const float position[3] = {0.0f, 0.0f, 0.0f};
        float transform[16];
        mu::content::placementTransform(angle[0] * degrees, angle[2] * degrees,
                                        -angle[1] * degrees, 1.0f, position, transform);

        for (const auto& point : points) {
            float turned[3];
            turnLikeMu(angle, point, turned);
            float wanted[3];
            toOurs(turned, wanted);

            float ourPoint[3];
            toOurs(point, ourPoint);
            // Row vector times the matrix, which is what the shader does.
            float got[3];
            for (int c = 0; c < 3; ++c) {
                got[c] = ourPoint[0] * transform[0 * 4 + c] + ourPoint[1] * transform[1 * 4 + c] +
                         ourPoint[2] * transform[2 * 4 + c] + transform[3 * 4 + c];
            }
            for (int c = 0; c < 3; ++c) {
                const float error = std::fabs(got[c] - wanted[c]);
                if (error > worstError) worstError = error;
                if (error > 1e-4f) {
                    char note[256];
                    std::snprintf(note, sizeof(note),
                                  "angles (%.0f %.0f %.0f), point (%.1f %.1f %.1f): axis %d is "
                                  "%.4f and MU says %.4f",
                                  angle[0], angle[1], angle[2], point[0], point[1], point[2], c,
                                  got[c], wanted[c]);
                    check(false, note);
                }
            }
        }
    }
    std::printf("placement_test: %zu angle sets x %zu points, worst error %.2e\n",
                sizeof(cases) / sizeof(cases[0]), sizeof(points) / sizeof(points[0]),
                double(worstError));

    // Scale and translation are the easy half, and are checked so that a change to them
    // cannot pass unnoticed behind the rotation.
    {
        const float position[3] = {3.0f, -4.0f, 5.0f};
        float transform[16];
        mu::content::placementTransform(0.0f, 0.0f, 0.0f, 2.0f, position, transform);
        const float point[3] = {1.0f, 1.0f, 1.0f};
        float got[3];
        for (int c = 0; c < 3; ++c) {
            got[c] = point[0] * transform[0 * 4 + c] + point[1] * transform[1 * 4 + c] +
                     point[2] * transform[2 * 4 + c] + transform[3 * 4 + c];
        }
        check(std::fabs(got[0] - 5.0f) < 1e-5f && std::fabs(got[1] + 2.0f) < 1e-5f &&
                  std::fabs(got[2] - 7.0f) < 1e-5f,
              "a scale of 2 about a position of (3, -4, 5)");
    }

    std::printf("placement_test: %d failures\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}
