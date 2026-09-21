namespace MuAssets.Numerics;

/// <summary>
/// A rigid transform: rotation then translation, with no scale.
/// </summary>
/// <remarks>
/// BMD bone transforms carry only rotation and translation, so storing a
/// quaternion plus a vector is exactly equivalent to the reference client's 3x4
/// matrices while staying trivially convertible to an engine transform.
/// </remarks>
public readonly struct MuTransform(MuQuaternion rotation, MuVec3 translation)
{
    public static readonly MuTransform Identity = new(MuQuaternion.Identity, default);

    public MuQuaternion Rotation { get; } = rotation;

    public MuVec3 Translation { get; } = translation;

    /// <summary>
    /// Composes a child transform into its parent's space — the equivalent of the
    /// reference client's matrix concatenation when resolving a bone hierarchy.
    /// </summary>
    public static MuTransform Concat(MuTransform parent, MuTransform child) => new(
        MuQuaternion.Multiply(parent.Rotation, child.Rotation),
        parent.Translation + parent.Rotation.Rotate(child.Translation));

    /// <summary>Applies rotation and translation to a point.</summary>
    public MuVec3 TransformPoint(MuVec3 point) => this.Translation + this.Rotation.Rotate(point);

    /// <summary>Applies rotation only — the correct treatment for normals.</summary>
    public MuVec3 TransformDirection(MuVec3 direction) => this.Rotation.Rotate(direction);

    public MuTransform Inverse()
    {
        var inverseRotation = this.Rotation.Conjugate();
        return new MuTransform(inverseRotation, -inverseRotation.Rotate(this.Translation));
    }

    public override string ToString() => $"rot={this.Rotation} pos={this.Translation}";
}
