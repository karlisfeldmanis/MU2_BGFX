using MuAssets.Cli;

// Gets MU's meshes and rigs out of the .bmd files and into the two formats MU2 keeps
// assets in: Wavefront OBJ for geometry, JSON for everything OBJ cannot hold.
//
// This is an import tool, not part of the build. It runs once per asset, by hand, and what
// it writes is committed — so a clone builds the Dark Knight without ever running it. It
// lives in MU2 all the same, because "MU2 needs nothing outside MU2" has to include being
// able to bring a *new* asset in.
if (args.Length == 0)
{
    Console.Error.WriteLine("""
        usage: muextract <command> [arguments]

          export-obj <bmdPath> <out.obj> [--action=N] [--key=N] [--split-bones]
              Bind-pose geometry as Wavefront OBJ, one group per MU mesh, each group
              named for the texture it wears. --action and --key bake against a chosen
              animation key instead, for an effect whose rest pose is not its picture.

          export-rig <bmdPath> <out.json> [--actions=1,4]
              The skeleton, the bone each built vertex is bound to, and the animation
              tracks. --actions selects which; the default is the rest pose alone, and
              'all' is rarely what anyone wants — player.bmd has 284.
        """);

    return 1;
}

try
{
    return args[0] switch
    {
        "export-obj" => ObjCommands.ExportObj(args[1..]),
        "export-rig" => RigCommands.ExportRig(args[1..]),
        _ => Unknown(args[0]),
    };
}
catch (Exception ex)
{
    Console.Error.WriteLine($"error: {ex.Message}");
    return 1;
}

static int Unknown(string command)
{
    Console.Error.WriteLine($"error: unknown command '{command}'");
    return 1;
}
