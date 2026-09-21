using System.Buffers.Binary;
using System.Globalization;

namespace MuAssets;

/// <summary>
/// A three-component vector in MU's own coordinate space: X and Y span the
/// terrain grid and Z is up. Deliberately independent of any engine type so the
/// decoders stay dependency-free; convert at the engine boundary.
/// </summary>
public readonly struct MuVec3(float x, float y, float z) : IEquatable<MuVec3>
{
    public const int ByteLength = 12;

    public float X { get; } = x;

    public float Y { get; } = y;

    /// <summary>Up, in MU's convention.</summary>
    public float Z { get; } = z;

    public static MuVec3 Read(ReadOnlySpan<byte> source) => new(
        BinaryPrimitives.ReadSingleLittleEndian(source),
        BinaryPrimitives.ReadSingleLittleEndian(source[4..]),
        BinaryPrimitives.ReadSingleLittleEndian(source[8..]));

    public static MuVec3 operator +(MuVec3 a, MuVec3 b) => new(a.X + b.X, a.Y + b.Y, a.Z + b.Z);

    public static MuVec3 operator -(MuVec3 a, MuVec3 b) => new(a.X - b.X, a.Y - b.Y, a.Z - b.Z);

    public static MuVec3 operator -(MuVec3 v) => new(-v.X, -v.Y, -v.Z);

    public static MuVec3 operator *(MuVec3 v, float scalar) => new(v.X * scalar, v.Y * scalar, v.Z * scalar);

    /// <summary>Linear interpolation, used to blend animation position keys.</summary>
    public static MuVec3 Lerp(MuVec3 a, MuVec3 b, float t) => new(
        a.X + ((b.X - a.X) * t),
        a.Y + ((b.Y - a.Y) * t),
        a.Z + ((b.Z - a.Z) * t));

    public float Length() => MathF.Sqrt((this.X * this.X) + (this.Y * this.Y) + (this.Z * this.Z));

    public MuVec3 Normalized()
    {
        var length = this.Length();
        return length < 1e-12f ? default : this * (1f / length);
    }

    public bool Equals(MuVec3 other)
        => this.X.Equals(other.X) && this.Y.Equals(other.Y) && this.Z.Equals(other.Z);

    public override bool Equals(object? obj) => obj is MuVec3 other && this.Equals(other);

    public override int GetHashCode() => HashCode.Combine(this.X, this.Y, this.Z);

    public override string ToString() => string.Format(
        CultureInfo.InvariantCulture, "({0:0.###}, {1:0.###}, {2:0.###})", this.X, this.Y, this.Z);
}
