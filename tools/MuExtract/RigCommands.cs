using System.Globalization;
using System.Text;
using MuAssets.Models;
using MuAssets.Numerics;

namespace MuAssets.Cli;

/// <summary>
/// Writes a model's skeleton, its per-vertex bone binding and its animation tracks as JSON.
/// </summary>
/// <remarks>
/// The one thing an OBJ cannot carry. MU2 keeps meshes as OBJ deliberately, and OBJ has no
/// notion of a skeleton, a bone weight or a keyframe — so the rig travels beside the mesh
/// in a file that does, and this is what writes it.
/// <para>
/// MU's skinning is rigid: <see cref="BmdVertex"/> is one bone index and a position already
/// in that bone's local space, with no weights anywhere. That is not a limitation to work
/// around — it maps exactly onto glTF as JOINTS_0 = [bone, 0, 0, 0] and WEIGHTS_0 =
/// [1, 0, 0, 0], and it means a vertex belongs to precisely one bone, which every later
/// step can rely on.
/// </para>
/// <para>
/// The division of labour is MU's own. A worn part carries its own skeleton and a single
/// bind-pose action; player.bmd carries no geometry at all and 284 actions. The bones match
/// by name and by index between them — both are a 3ds Max Biped, Bip01 downwards — so the
/// player's tracks drive the part's vertices, while the part's own rest pose is what the
/// inverse bind matrices come from. Exporting them separately is what keeps that straight.
/// </para>
/// <para>
/// Actions are selected rather than dumped. player.bmd holds 284 of them across 60 bones
/// and writing them all produces a file far larger than the model; <c>--actions=1,4</c>
/// asks for the male idle and the sword idle, which is what a standing Dark Knight needs.
/// </para>
/// </remarks>
internal static class RigCommands
{
    public static int ExportRig(string[] args)
    {
        if (args.Length < 2)
        {
            Console.Error.WriteLine(
                "usage: export-rig <bmdPath> <out.json> [--actions=1,4]");
            return 1;
        }

        var model = BmdReader.Load(args[0]);
        var wanted = ParseActions(args, model.Actions.Length);

        // Reordered and renamed on the way out, the same as the old client does on the way
        // in. Everything this file contains — the skeleton, the vertex bindings, the
        // tracks — is written in that order, so a consumer never sees a BMD index at all.
        var order = BoneOrder.Create(model);

        var json = new StringBuilder();
        json.Append("{\n");
        json.Append($"  \"source\": {Quote(Path.GetFileName(args[0]))},\n");
        json.Append($"  \"bones\": {order.Count},\n");
        json.Append($"  \"actions_available\": {model.Actions.Length},\n");

        WriteSkeleton(json, model, order);
        WriteMeshes(json, model, order);
        WriteActions(json, model, order, wanted);

        json.Append("}\n");

        File.WriteAllText(args[1], json.ToString());

        Console.WriteLine($"{args[0]}");
        Console.WriteLine(
            $"  bones={model.Bones.Length} meshes={model.Meshes.Length} "
            + $"actions={model.Actions.Length} exported={wanted.Count}");
        Console.WriteLine($"  wrote {args[1]} ({new FileInfo(args[1]).Length / 1024} KB)");
        return 0;
    }

    /// <summary>Which actions to write out, defaulting to the rest pose alone.</summary>
    private static List<int> ParseActions(string[] args, int available)
    {
        foreach (var argument in args)
        {
            if (!argument.StartsWith("--actions=", StringComparison.Ordinal))
            {
                continue;
            }

            var body = argument["--actions=".Length..];
            if (body == "all")
            {
                return Enumerable.Range(0, available).ToList();
            }

            return body.Split(',', StringSplitOptions.RemoveEmptyEntries)
                .Select(int.Parse)
                .Where(index => index >= 0 && index < available)
                .ToList();
        }

        return available > 0 ? [BmdPose.RestAction] : [];
    }

    /// <summary>
    /// The bone hierarchy, and each bone's local transform in the rest pose.
    /// </summary>
    /// <remarks>
    /// Local rather than resolved, because that is what an armature wants: a skeleton is
    /// built by parenting, and handing it world transforms means every child is placed
    /// twice. Dummy bones are written out too — they carry no name or animation, and MU
    /// keeps them so that sibling indices stay stable, which means dropping them here would
    /// silently renumber every bone after the first one.
    /// </remarks>
    private static void WriteSkeleton(StringBuilder json, BmdModel model, BoneOrder order)
    {
        json.Append("  \"skeleton\": [\n");

        for (var index = 0; index < order.Count; index++)
        {
            var bone = model.Bones[order.ToSource[index]];
            var local = BmdPose.LocalAt(bone, BmdPose.RestAction, BmdPose.RestKey);

            json.Append("    {");
            json.Append($"\"index\": {index}, ");
            json.Append($"\"name\": {Quote(order.Names[index])}, ");
            json.Append($"\"parent\": {order.ParentOf(model, index)}, ");
            json.Append($"\"source\": {order.ToSource[index]}, ");
            json.Append($"\"dummy\": {(bone.IsDummy ? "true" : "false")}, ");
            json.Append($"\"t\": {Vector(local.Translation)}, ");
            json.Append($"\"r\": {Quaternion(local.Rotation)}");
            json.Append(index == order.Count - 1 ? "}\n" : "},\n");
        }

        json.Append("  ],\n");
    }

    /// <summary>
    /// Which bone owns each vertex, per mesh, in the mesh's own vertex order.
    /// </summary>
    /// <remarks>
    /// The order matters and is the reason this is a bare array rather than anything
    /// friendlier: it is parallel to the positions the OBJ exporter wrote, so index N here
    /// describes vertex N there. Anything that reorders or merges vertices between the two
    /// breaks the correspondence silently — which is a real hazard, since welding duplicate
    /// vertices is exactly what the next step in the pipeline does.
    /// </remarks>
    private static void WriteMeshes(StringBuilder json, BmdModel model, BoneOrder order)
    {
        // Built geometry, not the raw BMD arrays, and this is the whole correctness of the
        // file. A BMD vertex is a position; a *rendered* vertex is a position with a normal
        // and a UV, so the builder splits one into several wherever those break. The chest
        // is 149 BMD vertices and 185 built ones.
        //
        // The OBJ exporter writes the built ones, in the builder's order, and
        // BmdMeshGeometry carries a BoneIndices array parallel to them. Taking the bone
        // from there is what makes this file line up with the OBJ index for index. Taking
        // it from mesh.Vertices — which is what this did first — produces an array of the
        // wrong length that still parses, still loads, and binds every vertex after the
        // first split to the wrong bone.
        var geometry = BmdMeshBuilder.Build(
            model, new BmdMeshBuildOptions(), new BmdMeshBuildReport());

        // The name clean_lowpoly joins these bones to the OBJ's groups by, which is not
        // always the texture's basename. See GroupNames.
        var groups = GroupNames.For(geometry);

        json.Append("  \"meshes\": [\n");

        for (var index = 0; index < geometry.Length; index++)
        {
            var mesh = geometry[index];

            json.Append("    {");
            json.Append($"\"texture\": {Quote(mesh.TextureName)}, ");
            json.Append($"\"group\": {Quote(groups[index])}, ");
            json.Append($"\"vertices\": {mesh.VertexCount}, ");
            json.Append("\"vertex_bones\": [");
            // Remapped into engine order here, so nothing downstream has to know that
            // BMD numbering exists. An unbound vertex is anchored to bone 0 rather than
            // left at -1, which is what the old client does and keeps it with the model
            // instead of at the origin.
            json.Append(string.Join(",", mesh.BoneIndices.Select(
                b => b >= 0 && b < order.ToEngine.Length ? order.ToEngine[b] : 0)));
            json.Append(']');
            json.Append(index == geometry.Length - 1 ? "}\n" : "},\n");
        }

        json.Append("  ],\n");
    }

    /// <summary>One track per bone per action: a local translation and rotation per key.</summary>
    private static void WriteActions(
        StringBuilder json, BmdModel model, BoneOrder order, List<int> wanted)
    {
        json.Append("  \"animations\": [\n");

        for (var slot = 0; slot < wanted.Count; slot++)
        {
            var action = wanted[slot];
            var keys = model.Actions[action].KeyCount;

            json.Append("    {");
            json.Append($"\"action\": {action}, \"keys\": {keys}, ");
            json.Append($"\"locked\": {(model.Actions[action].LockPositions ? "true" : "false")}, ");
            json.Append("\"tracks\": [");

            // Bones MU says nothing about in this action are left out entirely, rather
            // than written as a run of identical keys. A track that does not exist is not
            // a track of zeroes: the consumer must leave that bone at the skeleton's rest,
            // and it can only know to do that if the track is absent.
            var written = 0;

            for (var index = 0; index < order.Count; index++)
            {
                var bone = model.Bones[order.ToSource[index]];
                if (bone.IsDummy || action >= bone.Tracks.Length
                    || bone.Tracks[action].Positions.Length == 0)
                {
                    continue;
                }

                json.Append(written++ == 0 ? "\n" : ",\n");
                json.Append("      {");
                json.Append($"\"bone\": {index}, \"t\": [");

                for (var key = 0; key < keys; key++)
                {
                    var local = BmdPose.LocalAt(bone, action, key);
                    json.Append(key == 0 ? string.Empty : ",");
                    json.Append(Vector(local.Translation));
                }

                json.Append("], \"r\": [");

                for (var key = 0; key < keys; key++)
                {
                    var local = BmdPose.LocalAt(bone, action, key);
                    json.Append(key == 0 ? string.Empty : ",");
                    json.Append(Quaternion(local.Rotation));
                }

                json.Append("]}");
            }

            json.Append("\n    ]");
            json.Append(slot == wanted.Count - 1 ? "}\n" : "},\n");
        }

        json.Append("  ]\n");
    }

    /// <summary>
    /// A rotation, written as the quaternion the decoder already resolved it to.
    /// </summary>
    /// <remarks>
    /// MU stores Euler angles per key and BmdPose converts them; taking the converted form
    /// is not a convenience but a correctness point. glTF animates rotation as quaternions
    /// and interpolates them spherically, so writing Eulers here would mean converting
    /// twice with a different convention in the middle, which is how a shoulder ends up
    /// rotating the long way round between two keys.
    /// </remarks>
    private static string Quaternion(MuQuaternion q) =>
        "[" + F(q.X) + "," + F(q.Y) + "," + F(q.Z) + "," + F(q.W) + "]";

    private static string Vector(MuVec3 v) =>
        "[" + F(v.X) + "," + F(v.Y) + "," + F(v.Z) + "]";

    private static string F(float value) =>
        value.ToString("0.######", CultureInfo.InvariantCulture);

    private static string Quote(string value) =>
        "\"" + value.Replace("\\", "\\\\").Replace("\"", "\\\"") + "\"";
}
