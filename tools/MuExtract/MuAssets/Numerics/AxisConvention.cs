namespace MuAssets.Numerics;

/// <summary>
/// Converts between MU's Z-up space and the Y-up space Godot uses.
/// </summary>
/// <remarks>
/// MU is right-handed with X to the right, Y forward across the terrain grid and
/// Z up. Godot is right-handed with Y up and Z toward the viewer. Mapping
/// <c>(x, y, z)</c> to <c>(x, z, -y)</c> preserves handedness — the change of
/// basis has determinant +1 — so rotations carry across without mirroring.
/// <para>
/// Keep this the single place the conversion happens. A map spans 25,600 units,
/// so a sign error scattered through gameplay code is painful to unpick later.
/// </para>
/// </remarks>
public static class AxisConvention
{
    /// <summary>Converts a position from MU space to engine space.</summary>
    public static MuVec3 ToEngine(MuVec3 muPosition) => new(muPosition.X, muPosition.Z, -muPosition.Y);

    /// <summary>Converts a position from engine space back to MU space.</summary>
    public static MuVec3 ToMu(MuVec3 enginePosition) => new(enginePosition.X, -enginePosition.Z, enginePosition.Y);

    /// <summary>
    /// Converts a rotation from MU space to engine space. Because the change of
    /// basis is itself a rotation, only the axis needs remapping — the angle,
    /// and therefore W, is unchanged.
    /// </summary>
    public static MuQuaternion ToEngine(MuQuaternion muRotation)
        => new(muRotation.X, muRotation.Z, -muRotation.Y, muRotation.W);

    /// <summary>Converts a rigid transform from MU space to engine space.</summary>
    public static MuTransform ToEngine(MuTransform muTransform)
        => new(ToEngine(muTransform.Rotation), ToEngine(muTransform.Translation));

    /// <summary>Degrees to radians, for the Euler angles stored in object placement records.</summary>
    public static MuVec3 DegreesToRadians(MuVec3 degrees)
    {
        const float factor = MathF.PI / 180f;
        return new MuVec3(degrees.X * factor, degrees.Y * factor, degrees.Z * factor);
    }
}
