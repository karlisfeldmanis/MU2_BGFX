#pragma once

// The rules' own sin, cos, atan2 and hypot, the same bits on every machine.
//
// The server runs on Linux x86 and the client on this Mac's ARM, and from server-plan phase 4 the
// client's mirror steps the same realm from the same commands (lockstep, docs/sprints/18-the-wire.md).
// That holds only if every number the rules compute comes out identical on both. The C library's
// transcendental functions do not: Apple's libm and glibc round differently in the last bit, and
// a seeded fight drifted by a tick over 15,000 ticks (server/README.md, 2026-10-07). So the rules
// use these instead: fdlibm's kernels (Sun, 1993, the source of most C libraries' own), computed
// in double with only +, * and /, in a fixed order. With fused multiply-add off for the whole
// build (CMakeLists.txt, -ffp-contract=off), IEEE arithmetic gives the same answer everywhere.
// sqrt, floor, fmod and a power of two are exact in IEEE and stay the library's.
//
// Accurate to about one unit in the last place of a double, far past what a float rule needs.

#include <cmath>

namespace mu::sim::fm {

namespace detail {

// fdlibm's __kernel_sin and __kernel_cos on [-pi/4, pi/4].
inline double kernelSin(double x) {
    constexpr double S1 = -1.66666666666666324348e-01, S2 = 8.33333333332248946124e-03,
                     S3 = -1.98412698298579493134e-04, S4 = 2.75573137070700676789e-06,
                     S5 = -2.50507602534068634195e-08, S6 = 1.58969099521155010221e-10;
    const double z = x * x;
    const double v = z * x;
    const double r = S2 + z * (S3 + z * (S4 + z * (S5 + z * S6)));
    return x + v * (S1 + z * r);
}

inline double kernelCos(double x) {
    constexpr double C1 = 4.16666666666666019037e-02, C2 = -1.38888888888741095749e-03,
                     C3 = 2.48015872894767294178e-05, C4 = -2.75573143513906633035e-07,
                     C5 = 2.08757232129817482790e-09, C6 = -1.13596475577881948265e-11;
    const double z = x * x;
    const double r = z * (C1 + z * (C2 + z * (C3 + z * (C4 + z * (C5 + z * C6)))));
    return 1.0 - (0.5 * z - z * r);
}

// x = k * pi/2 + r, |r| <= pi/4, by Cody and Waite's two-part pi/2: exact for the angles a
// game turns through (well under 2^20 radians).
inline double reduce(double x, int& quadrant) {
    constexpr double kInvPio2 = 6.36619772367581382433e-01;
    constexpr double kPio2Hi = 1.57079632673412561417e+00;
    constexpr double kPio2Lo = 6.07710050650619224932e-11;
    const double k = std::floor(x * kInvPio2 + 0.5);
    quadrant = int(static_cast<long long>(k) & 3);
    return (x - k * kPio2Hi) - k * kPio2Lo;
}

// fdlibm's atan.
inline double atan(double x) {
    constexpr double kHi[] = {4.63647609000806093515e-01, 7.85398163397448278999e-01,
                              9.82793723247329054082e-01, 1.57079632679489655800e+00};
    constexpr double kLo[] = {2.26987774529616870924e-17, 3.06161699786838301793e-17,
                              1.39033110312309984516e-17, 6.12323399573676603587e-17};
    constexpr double kT[] = {3.33333333333329318027e-01,  -1.99999999998764832476e-01,
                             1.42857142725034663711e-01,  -1.11111104054623557880e-01,
                             9.09088713343650656196e-02,  -7.69187620504482999495e-02,
                             6.66107313738753120669e-02,  -5.83357013379057348645e-02,
                             4.97687799461593236017e-02,  -3.65315727442169155270e-02,
                             1.62858201153657823623e-02};
    const bool negative = x < 0.0;
    double a = negative ? -x : x;
    int id = -1;
    if (a >= 0.4375) {
        if (a < 1.1875) {
            if (a < 0.6875) {
                id = 0;
                a = (2.0 * a - 1.0) / (2.0 + a);
            } else {
                id = 1;
                a = (a - 1.0) / (a + 1.0);
            }
        } else if (a < 2.4375) {
            id = 2;
            a = (a - 1.5) / (1.0 + 1.5 * a);
        } else {
            id = 3;
            a = -1.0 / a;
        }
    }
    const double z = a * a;
    const double w = z * z;
    const double s1 = z * (kT[0] + w * (kT[2] + w * (kT[4] + w * (kT[6] + w * (kT[8] + w * kT[10])))));
    const double s2 = w * (kT[1] + w * (kT[3] + w * (kT[5] + w * (kT[7] + w * kT[9]))));
    if (id < 0) return negative ? -(a - a * (s1 + s2)) : a - a * (s1 + s2);
    const double r = kHi[id] - ((a * (s1 + s2) - kLo[id]) - a);
    return negative ? -r : r;
}

}  // namespace detail

inline double sin(double x) {
    int q = 0;
    const double r = detail::reduce(x, q);
    switch (q) {
        case 0: return detail::kernelSin(r);
        case 1: return detail::kernelCos(r);
        case 2: return -detail::kernelSin(r);
        default: return -detail::kernelCos(r);
    }
}

inline double cos(double x) {
    int q = 0;
    const double r = detail::reduce(x, q);
    switch (q) {
        case 0: return detail::kernelCos(r);
        case 1: return -detail::kernelSin(r);
        case 2: return -detail::kernelCos(r);
        default: return detail::kernelSin(r);
    }
}

inline double atan2(double y, double x) {
    constexpr double kPi = 3.14159265358979311600e+00;
    constexpr double kHalfPi = 1.57079632679489655800e+00;
    if (x == 0.0) return y > 0.0 ? kHalfPi : y < 0.0 ? -kHalfPi : 0.0;
    const double a = detail::atan(y / x);
    if (x > 0.0) return a;
    return y >= 0.0 ? a + kPi : a - kPi;
}

inline double hypot(double x, double y) { return std::sqrt(x * x + y * y); }

// The float forms the rules mostly call, computed in double and rounded once.
inline float sin(float x) { return float(sin(double(x))); }
inline float cos(float x) { return float(cos(double(x))); }
inline float atan2(float y, float x) { return float(atan2(double(y), double(x))); }
inline float hypot(float x, float y) { return float(hypot(double(x), double(y))); }

}  // namespace mu::sim::fm
