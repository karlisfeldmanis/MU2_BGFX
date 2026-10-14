using System.Globalization;

namespace MuAssets.Numerics;

/// <summary>
/// A unit quaternion in MU's coordinate space.
/// </summary>
public readonly struct MuQuaternion(float x, float y, float z, float w)
{
    public static readonly MuQuaternion Identity = new(0f, 0f, 0f, 1f);

    public float X { get; } = x;

    public float Y { get; } = y;

    public float Z { get; } = z;

    public float W { get; } = w;

    /// <summary>
    /// Builds a rotation from MU's Euler angles, which are stored in radians.
    /// </summary>
    /// <remarks>
    /// The composition is Rz * Ry * Rx — that is, X is applied first and Z last.
    /// This mirrors the reference client's angle-to-quaternion conversion exactly;
    /// deriving it from a generic Euler helper is a reliable way to get subtly
    /// wrong limb rotations, because engines disagree on what "XYZ order" means.
    /// </remarks>
    public static MuQuaternion FromEulerRadians(MuVec3 angles)
    {
        var halfX = angles.X * 0.5f;
        var halfY = angles.Y * 0.5f;
        var halfZ = angles.Z * 0.5f;

        var sinX = MathF.Sin(halfX);
        var cosX = MathF.Cos(halfX);
        var sinY = MathF.Sin(halfY);
        var cosY = MathF.Cos(halfY);
        var sinZ = MathF.Sin(halfZ);
        var cosZ = MathF.Cos(halfZ);

        return new MuQuaternion(
            (sinX * cosY * cosZ) - (cosX * sinY * sinZ),
            (cosX * sinY * cosZ) + (sinX * cosY * sinZ),
            (cosX * cosY * sinZ) - (sinX * sinY * cosZ),
            (cosX * cosY * cosZ) + (sinX * sinY * sinZ));
    }

    /// <summary>Hamilton product. <c>a * b</c> applies <paramref name="b"/> first.</summary>
    public static MuQuaternion Multiply(MuQuaternion a, MuQuaternion b) => new(
        (a.W * b.X) + (a.X * b.W) + (a.Y * b.Z) - (a.Z * b.Y),
        (a.W * b.Y) - (a.X * b.Z) + (a.Y * b.W) + (a.Z * b.X),
        (a.W * b.Z) + (a.X * b.Y) - (a.Y * b.X) + (a.Z * b.W),
        (a.W * b.W) - (a.X * b.X) - (a.Y * b.Y) - (a.Z * b.Z));

    /// <summary>Inverse rotation. Valid for unit quaternions only.</summary>
    public MuQuaternion Conjugate() => new(-this.X, -this.Y, -this.Z, this.W);

    public float Length() => MathF.Sqrt((this.X * this.X) + (this.Y * this.Y) + (this.Z * this.Z) + (this.W * this.W));

    public MuQuaternion Normalized()
    {
        var length = this.Length();
        if (length < 1e-12f)
        {
            return Identity;
        }

        var scale = 1f / length;
        return new MuQuaternion(this.X * scale, this.Y * scale, this.Z * scale, this.W * scale);
    }

    /// <summary>Rotates a vector by this quaternion.</summary>
    public MuVec3 Rotate(MuVec3 v)
    {
        // t = 2 * (q_vec x v); result = v + w * t + q_vec x t
        var tx = 2f * ((this.Y * v.Z) - (this.Z * v.Y));
        var ty = 2f * ((this.Z * v.X) - (this.X * v.Z));
        var tz = 2f * ((this.X * v.Y) - (this.Y * v.X));

        return new MuVec3(
            v.X + (this.W * tx) + ((this.Y * tz) - (this.Z * ty)),
            v.Y + (this.W * ty) + ((this.Z * tx) - (this.X * tz)),
            v.Z + (this.W * tz) + ((this.X * ty) - (this.Y * tx)));
    }

    /// <summary>
    /// Normalised linear interpolation, matching the reference client's blend.
    /// Takes the shorter arc, so opposing-sign quaternions do not spin the long way.
    /// </summary>
    public static MuQuaternion Nlerp(MuQuaternion a, MuQuaternion b, float t)
    {
        var dot = (a.X * b.X) + (a.Y * b.Y) + (a.Z * b.Z) + (a.W * b.W);
        var sign = dot < 0f ? -1f : 1f;

        return new MuQuaternion(
            a.X + ((b.X * sign) - a.X) * t,
            a.Y + ((b.Y * sign) - a.Y) * t,
            a.Z + ((b.Z * sign) - a.Z) * t,
            a.W + ((b.W * sign) - a.W) * t).Normalized();
    }

    public override string ToString() => string.Format(
        CultureInfo.InvariantCulture, "({0:0.####}, {1:0.####}, {2:0.####}, {3:0.####})", this.X, this.Y, this.Z, this.W);
}
