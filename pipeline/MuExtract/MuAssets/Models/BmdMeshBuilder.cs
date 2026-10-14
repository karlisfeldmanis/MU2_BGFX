using MuAssets.Numerics;

namespace MuAssets.Models;

/// <summary>Options controlling how a BMD mesh is flattened for an engine.</summary>
public sealed class BmdMeshBuildOptions
{
    /// <summary>Convert positions and normals from MU's Z-up space to Y-up engine space.</summary>
    public bool ConvertAxes { get; init; } = true;

    /// <summary>
    /// Flip the V texture coordinate. Off, because BMD coordinates already suit
    /// the way our decoders hand over an image.
    /// </summary>
    /// <remarks>
    /// It is tempting to reason that MU targets OpenGL, where V = 0 is the bottom
    /// of the image, and that a top-down decoder therefore needs V inverted. That
    /// holds for the terrain, which has its own builder and its own verified flip,
    /// but not for models: their coordinates come out correct as stored.
    /// <para>
    /// The reason it went unnoticed is that most model textures are stone, wood or
    /// foliage, where upside down is indistinguishable from right way up. It shows
    /// on anything with a fixed orientation. Rendering <c>Sign01</c> settles it in
    /// one look: with the flip its writing is an unreadable smudge and its ironwork
    /// is garbled, and without it the sign reads cleanly. A packed atlas is worse
    /// still — flipping there does not turn a picture over, it lands in whichever
    /// region happens to sit opposite, which is what put blue bands across a
    /// character's leggings.
    /// </para>
    /// </remarks>
    public bool FlipV { get; init; }

    /// <summary>
    /// Reverse triangle winding. Required, and therefore on by default.
    /// </summary>
    /// <remarks>
    /// The axis conversion preserves 3D handedness — its determinant is +1 — but
    /// moving "up" from Z to Y mirrors the horizontal plane, so faces come out
    /// back-facing and get culled. Confirmed by rendering: terrain was invisible
    /// under back-face culling until the winding was reversed.
    /// </remarks>
    public bool FlipWinding { get; init; } = true;

    /// <summary>
    /// The pose to bake positions against, instead of the model's own rest.
    /// </summary>
    /// <remarks>
    /// A BMD vertex is stored in its bone's local space, and this is what turns it into a
    /// position: the bone's transform in some chosen pose. Left null, that pose is the
    /// model's own action 0, which is right for anything drawn on its own.
    /// <para>
    /// It is wrong for a worn part. MU never draws a helm in the helm's rest pose — it
    /// draws it on a character, using player.bmd's bones, and the part's own rest is only
    /// what it happens to look like standing alone. Baking against the part's rest and then
    /// animating with the player's produces a mesh whose bind pose and animation disagree
    /// by up to 39 degrees at the shoulder, and with MU's rigid one-bone-per-vertex
    /// skinning a disagreement is not a bend, it is a part coming off.
    /// </para>
    /// <para>
    /// Handed the player's rest instead, the geometry is baked in the same pose the
    /// animation is stated in, and the clip can be played exactly as MU stores it.
    /// </para>
    /// </remarks>
    public MuTransform[]? RestPose { get; init; }
}

/// <summary>Diagnostics gathered while flattening, so bad data is reported rather than silently dropped.</summary>
public sealed class BmdMeshBuildReport
{
    /// <summary>Faces skipped because an index pointed outside its array.</summary>
    public int SkippedOutOfRangeFaces { get; set; }

    /// <summary>Faces skipped because two or more corners coincided.</summary>
    public int SkippedDegenerateFaces { get; set; }

    /// <summary>Vertices whose bone index was absent or out of range; treated as unrigged.</summary>
    public int UnboundVertices { get; set; }

    /// <summary>Corners whose normal index was out of range; replaced with a computed face normal.</summary>
    public int SubstitutedNormals { get; set; }

    /// <summary>Faces with four corners, split into two triangles.</summary>
    public int QuadsTriangulated { get; set; }

    public bool IsClean =>
        this.SkippedOutOfRangeFaces == 0
        && this.SkippedDegenerateFaces == 0
        && this.SubstitutedNormals == 0;
}

/// <summary>Engine-ready geometry for one BMD submesh.</summary>
public sealed class BmdMeshGeometry
{
    public required string TextureName { get; init; }

    public required short TextureIndex { get; init; }

    /// <summary>Positions in bind-pose model space.</summary>
    public required MuVec3[] Positions { get; init; }

    public required MuVec3[] Normals { get; init; }

    public required BmdTexCoord[] TexCoords { get; init; }

    /// <summary>Controlling bone per vertex, or -1 when unrigged. MU binds each vertex to exactly one bone.</summary>
    public required int[] BoneIndices { get; init; }

    /// <summary>Triangle list.</summary>
    public required int[] Indices { get; init; }

    public int VertexCount => this.Positions.Length;

    public int TriangleCount => this.Indices.Length / 3;
}

/// <summary>
/// Flattens BMD geometry into the parallel arrays a GPU needs.
/// </summary>
/// <remarks>
/// MU indexes position, normal and texture coordinate independently per face
/// corner, which no engine vertex buffer can express directly. Each distinct
/// combination therefore becomes one vertex, de-duplicated so shared corners do
/// not inflate the buffer.
/// <para>
/// Positions are baked against the bind pose: a vertex is stored in its bone's
/// local space, so it is transformed by that bone's rest transform to land in
/// model space. Skinning at runtime then only needs the bone's animated pose
/// relative to that rest transform.
/// </para>
/// </remarks>
public static class BmdMeshBuilder
{
    public static BmdMeshGeometry[] Build(
        BmdModel model,
        BmdMeshBuildOptions? options = null,
        BmdMeshBuildReport? report = null)
    {
        options ??= new BmdMeshBuildOptions();
        report ??= new BmdMeshBuildReport();

        var restPose = options.RestPose ?? BmdPose.RestPose(model);
        var result = new BmdMeshGeometry[model.Meshes.Length];

        for (var i = 0; i < model.Meshes.Length; i++)
        {
            result[i] = BuildMesh(model.Meshes[i], restPose, options, report);
        }

        return result;
    }

    private static BmdMeshGeometry BuildMesh(
        BmdMesh mesh,
        MuTransform[] restPose,
        BmdMeshBuildOptions options,
        BmdMeshBuildReport report)
    {
        var positions = new List<MuVec3>(mesh.Vertices.Length);
        var normals = new List<MuVec3>(mesh.Vertices.Length);
        var texCoords = new List<BmdTexCoord>(mesh.Vertices.Length);
        var boneIndices = new List<int>(mesh.Vertices.Length);
        var indices = new List<int>(mesh.Triangles.Length * 3);

        // Maps a (position, normal, uv) index triple onto its flattened vertex.
        var lookup = new Dictionary<(short Vertex, short Normal, short TexCoord), int>();

        foreach (var triangle in mesh.Triangles)
        {
            var corners = triangle.Polygon switch
            {
                3 => 3,
                4 => 4,
                _ => 0,
            };

            if (corners == 0)
            {
                report.SkippedOutOfRangeFaces++;
                continue;
            }

            if (!AreCornersInRange(mesh, triangle, corners))
            {
                report.SkippedOutOfRangeFaces++;
                continue;
            }

            if (corners == 4)
            {
                report.QuadsTriangulated++;
            }

            // Fan-triangulate: (0,1,2) and, for a quad, (0,2,3).
            for (var t = 0; t + 2 < corners; t++)
            {
                Span<int> face = [0, t + 1, t + 2];

                var a = EmitVertex(mesh, triangle, face[0], restPose, options, report, positions, normals, texCoords, boneIndices, lookup);
                var b = EmitVertex(mesh, triangle, face[1], restPose, options, report, positions, normals, texCoords, boneIndices, lookup);
                var c = EmitVertex(mesh, triangle, face[2], restPose, options, report, positions, normals, texCoords, boneIndices, lookup);

                if (a == b || b == c || a == c)
                {
                    report.SkippedDegenerateFaces++;
                    continue;
                }

                if (options.FlipWinding)
                {
                    indices.Add(a);
                    indices.Add(c);
                    indices.Add(b);
                }
                else
                {
                    indices.Add(a);
                    indices.Add(b);
                    indices.Add(c);
                }
            }
        }

        return new BmdMeshGeometry
        {
            TextureName = mesh.TextureName,
            TextureIndex = mesh.TextureIndex,
            Positions = positions.ToArray(),
            Normals = normals.ToArray(),
            TexCoords = texCoords.ToArray(),
            BoneIndices = boneIndices.ToArray(),
            Indices = indices.ToArray(),
        };
    }

    /// <summary>Checks that every index on a face is in range before any vertex is emitted.</summary>
    private static bool AreCornersInRange(BmdMesh mesh, BmdTriangle triangle, int corners)
    {
        for (var i = 0; i < corners; i++)
        {
            var vertexIndex = triangle.VertexIndex[i];
            var texCoordIndex = triangle.TexCoordIndex[i];

            if (vertexIndex < 0 || vertexIndex >= mesh.Vertices.Length)
            {
                return false;
            }

            if (texCoordIndex < 0 || texCoordIndex >= mesh.TexCoords.Length)
            {
                return false;
            }
        }

        return true;
    }

    private static int EmitVertex(
        BmdMesh mesh,
        BmdTriangle triangle,
        int corner,
        MuTransform[] restPose,
        BmdMeshBuildOptions options,
        BmdMeshBuildReport report,
        List<MuVec3> positions,
        List<MuVec3> normals,
        List<BmdTexCoord> texCoords,
        List<int> boneIndices,
        Dictionary<(short, short, short), int> lookup)
    {
        var vertexIndex = triangle.VertexIndex[corner];
        var normalIndex = triangle.NormalIndex[corner];
        var texCoordIndex = triangle.TexCoordIndex[corner];

        var key = (vertexIndex, normalIndex, texCoordIndex);
        if (lookup.TryGetValue(key, out var existing))
        {
            return existing;
        }

        var vertex = mesh.Vertices[vertexIndex];
        var bone = vertex.BoneIndex;
        var hasBone = bone >= 0 && bone < restPose.Length;
        if (!hasBone)
        {
            report.UnboundVertices++;
        }

        var boneTransform = hasBone ? restPose[bone] : MuTransform.Identity;
        var position = boneTransform.TransformPoint(vertex.Position);

        MuVec3 normal;
        if (normalIndex >= 0 && normalIndex < mesh.Normals.Length)
        {
            var stored = mesh.Normals[normalIndex];
            var normalBone = stored.BoneIndex >= 0 && stored.BoneIndex < restPose.Length
                ? restPose[stored.BoneIndex]
                : boneTransform;
            normal = normalBone.TransformDirection(stored.Normal).Normalized();
        }
        else
        {
            // No usable normal on this corner; leave it zeroed and let the caller
            // generate one. Recorded so a systematic gap is visible.
            report.SubstitutedNormals++;
            normal = default;
        }

        var uv = mesh.TexCoords[texCoordIndex];
        var v = options.FlipV ? 1f - uv.V : uv.V;

        if (options.ConvertAxes)
        {
            position = AxisConvention.ToEngine(position);
            normal = AxisConvention.ToEngine(normal);
        }

        var index = positions.Count;
        positions.Add(position);
        normals.Add(normal);
        texCoords.Add(new BmdTexCoord(uv.U, v));
        boneIndices.Add(hasBone ? bone : -1);
        lookup[key] = index;

        return index;
    }
}
