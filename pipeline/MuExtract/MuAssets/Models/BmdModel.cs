namespace MuAssets.Models;

/// <summary>A vertex bound rigidly to a single bone.</summary>
/// <param name="BoneIndex">Index of the controlling bone; -1 means unbound.</param>
/// <param name="Position">Position in that bone's local space.</param>
public readonly record struct BmdVertex(short BoneIndex, MuVec3 Position)
{
    public const int ByteLength = 16;
}

/// <summary>A normal, also bone-bound, plus a back-reference to the vertex it belongs to.</summary>
public readonly record struct BmdNormal(short BoneIndex, MuVec3 Normal, short BindVertex)
{
    public const int ByteLength = 20;
}

/// <summary>A UV pair.</summary>
public readonly record struct BmdTexCoord(float U, float V)
{
    public const int ByteLength = 8;
}

/// <summary>
/// One face. MU stores four-element index arrays regardless of the actual
/// polygon degree; <see cref="Polygon"/> says how many are live (3 in practice).
/// </summary>
public sealed class BmdTriangle
{
    /// <summary>Bytes each face occupies on disk, including struct padding.</summary>
    public const int ByteLength = 64;

    /// <summary>Vertex count for this face — 3 for every face in the shipped data.</summary>
    public byte Polygon { get; init; }

    public short[] VertexIndex { get; init; } = new short[4];

    public short[] NormalIndex { get; init; } = new short[4];

    public short[] TexCoordIndex { get; init; } = new short[4];

    public BmdTexCoord[] LightMapCoord { get; init; } = new BmdTexCoord[4];

    public short LightMapIndex { get; init; }
}

/// <summary>A submesh with a single texture.</summary>
public sealed class BmdMesh
{
    public short TextureIndex { get; init; }

    /// <summary>Texture file name as stored in the model, e.g. <c>skin.jpg</c>.</summary>
    public string TextureName { get; init; } = string.Empty;

    public BmdVertex[] Vertices { get; init; } = [];

    public BmdNormal[] Normals { get; init; } = [];

    public BmdTexCoord[] TexCoords { get; init; } = [];

    public BmdTriangle[] Triangles { get; init; } = [];
}

/// <summary>An animation clip. Keys are uniformly spaced.</summary>
public sealed class BmdAction
{
    public short KeyCount { get; init; }

    /// <summary>When set, the clip carries its own root translation track.</summary>
    public bool LockPositions { get; init; }

    /// <summary>Root positions, one per key; empty unless <see cref="LockPositions"/> is set.</summary>
    public MuVec3[] Positions { get; init; } = [];
}

/// <summary>Per-action transform track for one bone.</summary>
public sealed class BmdBoneTrack
{
    /// <summary>One translation per animation key.</summary>
    public MuVec3[] Positions { get; init; } = [];

    /// <summary>One Euler rotation (radians) per animation key.</summary>
    public MuVec3[] Rotations { get; init; } = [];
}

/// <summary>
/// A skeleton joint. "Dummy" bones are placeholders that keep sibling indices
/// stable and carry no name, parent or animation data.
/// </summary>
public sealed class BmdBone
{
    public bool IsDummy { get; init; }

    public string Name { get; init; } = string.Empty;

    /// <summary>Parent bone index, or -1 for a root.</summary>
    public short Parent { get; init; } = -1;

    /// <summary>One track per action, parallel to <see cref="BmdModel.Actions"/>.</summary>
    public BmdBoneTrack[] Tracks { get; init; } = [];
}

/// <summary>
/// A parsed .bmd model: geometry, skeleton and animation clips.
/// </summary>
public sealed class BmdModel
{
    /// <summary>Internal model name, normally the original .smd filename.</summary>
    public string Name { get; init; } = string.Empty;

    /// <summary>File format version: 10 (plain) or 12 (encrypted payload).</summary>
    public byte Version { get; init; }

    public BmdMesh[] Meshes { get; init; } = [];

    public BmdBone[] Bones { get; init; } = [];

    public BmdAction[] Actions { get; init; } = [];

    public int TotalVertices => this.Meshes.Sum(m => m.Vertices.Length);

    public int TotalTriangles => this.Meshes.Sum(m => m.Triangles.Length);
}
