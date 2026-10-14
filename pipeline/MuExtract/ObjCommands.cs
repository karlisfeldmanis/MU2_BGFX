using MuAssets;
using MuAssets.Models;
using MuAssets.Numerics;

namespace MuAssets.Cli;

/// <summary>Writes a BMD's bind-pose geometry as Wavefront OBJ.</summary>
/// <remarks>
/// One group per MU mesh, named for the texture that mesh wears. That grouping is the only
/// record of which faces sample which sheet, and MU2's pipeline reads it back — see the
/// note in clean_lowpoly on why Blender throws it away by default.
/// </remarks>
internal static class ObjCommands
{
    public static int ExportObj(string[] args)
    {
        if (args.Length < 2)
        {
            Console.Error.WriteLine(
                "usage: export-obj <bmdPath> <outObjPath> [--action=N] [--key=N] [--split-bones]");
            return 1;
        }

        // Which pose to bake against, and the default is the bind pose this command has
        // always written — action 0, key 0, which is what BmdPose calls the rest.
        //
        // <b>An effect model's rest is not always its picture</b>, and that is why this exists.
        // Fire01's rest is the meteor as it is drawn and its one action only makes the flame
        // writhe, so geometry alone was enough for it. Ice01's rest is the *first* key of a
        // six-key action whose bones translate the shards inward — the block unformed — so a
        // bind-pose export of it draws a shape MU never shows. See client/core/Ice.cs, which
        // steps six of these one per reference frame the way MoveEffect steps the animation.
        //
        // Baking a key rather than skinning at run time is the choice made for it: the whole
        // animation is six keys of one action played once at a fixed speed and frozen on the
        // last, so what a rig would buy is the ability to blend and interpolate something that
        // never blends. The effects folder still carries geometry and nothing else.
        var action = Option(args, "--action") ?? BmdPose.RestAction;
        var key = Option(args, "--key") ?? BmdPose.RestKey;

        var model = BmdReader.Load(args[0]);

        var geometry = BmdMeshBuilder.Build(
            model,
            new BmdMeshBuildOptions
            {
                RestPose = action == BmdPose.RestAction && key == BmdPose.RestKey
                    ? null
                    : BmdPose.ResolveGlobal(model, action, key),
            },
            new BmdMeshBuildReport());

        var output = new StringWriter();
        output.WriteLine(
            $"# {Path.GetFileName(args[0])} — MU geometry at action {action} key {key}, "
            + "exported for MU2");
        output.WriteLine($"# {geometry.Length} mesh(es)");

        // OBJ indices are one-based and run across the whole file rather than per group,
        // so each mesh's vertices are offset by everything written before it.
        var vertexBase = 1;
        var normalBase = 1;
        var texBase = 1;

        // One name per mesh, shared with the rig writer. See GroupNames.
        var groups = GroupNames.For(geometry);

        // One group per bone as well as per mesh, where asked: `name_bN`.
        //
        // For an effect whose bones only turn rigid layers — MagicCircle01's two squares and two
        // rings, on Box01 and Box03, counter-rotating a quarter turn a key — so the drawing can
        // spin each group about its own axis instead of carrying a skeleton. See
        // client/core/Summoning.cs.
        var splitBones = args.Contains("--split-bones");

        foreach (var (mesh, index) in geometry.Select((m, i) => (m, i)))
        {
            var group = groups[index];

            output.WriteLine();

            if (!splitBones)
            {
                output.WriteLine($"g {group}");
            }

            output.WriteLine($"# texture: {mesh.TextureName}");

            foreach (var p in mesh.Positions)
            {
                output.WriteLine($"v {F(p.X)} {F(p.Y)} {F(p.Z)}");
            }

            foreach (var n in mesh.Normals)
            {
                output.WriteLine($"vn {F(n.X)} {F(n.Y)} {F(n.Z)}");
            }

            // V flipped: MU's texture origin is the top-left and OBJ's is the bottom-left,
            // so writing the coordinate through unchanged mirrors every sheet vertically.
            foreach (var t in mesh.TexCoords)
            {
                output.WriteLine($"vt {F(t.U)} {F(1f - t.V)}");
            }

            var bones = splitBones
                ? mesh.BoneIndices.Distinct().Order().ToArray()
                : [-1];

            foreach (var bone in bones)
            {
                if (splitBones)
                {
                    output.WriteLine($"g {group}_b{bone}");
                }

                for (var i = 0; i + 2 < mesh.Indices.Length; i += 3)
                {
                    if (splitBones && mesh.BoneIndices[mesh.Indices[i]] != bone)
                    {
                        continue;
                    }

                    var a = mesh.Indices[i] + vertexBase;
                    var b = mesh.Indices[i + 1] + vertexBase;
                    var c = mesh.Indices[i + 2] + vertexBase;

                    var at = mesh.Indices[i] + texBase;
                    var bt = mesh.Indices[i + 1] + texBase;
                    var ct = mesh.Indices[i + 2] + texBase;

                    var an = mesh.Indices[i] + normalBase;
                    var bn = mesh.Indices[i + 1] + normalBase;
                    var cn = mesh.Indices[i + 2] + normalBase;

                    output.WriteLine($"f {a}/{at}/{an} {b}/{bt}/{bn} {c}/{ct}/{cn}");
                }
            }

            vertexBase += mesh.Positions.Length;
            normalBase += mesh.Normals.Length;
            texBase += mesh.TexCoords.Length;
        }

        var destination = args[1];
        Directory.CreateDirectory(Path.GetDirectoryName(Path.GetFullPath(destination))!);
        File.WriteAllText(destination, output.ToString());

        var triangles = geometry.Sum(m => m.TriangleCount);
        var vertices = geometry.Sum(m => m.VertexCount);
        Console.WriteLine(
            $"{Path.GetFileName(args[0])} -> {destination}\n"
            + $"  {geometry.Length} mesh(es), {vertices} vertices, {triangles} triangles");

        foreach (var mesh in geometry)
        {
            Console.WriteLine($"    {mesh.TextureName,-24} {mesh.TriangleCount,6} tris");
        }

        return 0;

        static string F(float value) =>
            value.ToString("0.######", System.Globalization.CultureInfo.InvariantCulture);
    }

    /// <summary>One <c>--name=value</c> as a number, or nothing where it was not passed.</summary>
    private static int? Option(string[] args, string name)
    {
        foreach (var one in args)
        {
            if (one.StartsWith($"{name}=", StringComparison.Ordinal)
                && int.TryParse(one[(name.Length + 1)..], out var value))
            {
                return value;
            }
        }

        return null;
    }
}
