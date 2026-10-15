using MuAssets.Models;

namespace MuAssets.Cli;

/// <summary>Reads every .bmd under a folder and says which do not come out as a model.</summary>
/// <remarks>
/// Written to prove the version-14 decryption (ModulusCrypt): a wrong cipher can still
/// hand the parser counts it accepts, so a file passes only when its name is printable and
/// every vertex and normal is bound to a bone the file has. MU's data tables (Local/*.bmd,
/// Item/item.bmd) share the extension without being models; they fail on the signature and
/// are counted apart.
/// </remarks>
internal static class CheckCommands
{
    public static int CheckBmd(string[] args)
    {
        if (args.Length < 1)
        {
            Console.Error.WriteLine("usage: check-bmd <folder>");
            return 1;
        }

        var byVersion = new SortedDictionary<int, (int Read, int Failed)>();
        var notModels = 0;
        var failures = new List<string>();

        foreach (var path in Directory.EnumerateFiles(args[0], "*", SearchOption.AllDirectories)
                     .Where(p => p.EndsWith(".bmd", StringComparison.OrdinalIgnoreCase))
                     .Order(StringComparer.Ordinal))
        {
            var head = new byte[4];
            using (var stream = File.OpenRead(path))
            {
                if (stream.Read(head) < 4 || head[0] != 'B' || head[1] != 'M' || head[2] != 'D')
                {
                    notModels++;
                    continue;
                }
            }

            var (read, failed) = byVersion.GetValueOrDefault(head[3]);
            string? why;
            try
            {
                why = Implausible(BmdReader.Load(path));
            }
            catch (Exception ex)
            {
                why = ex.InnerException?.Message ?? ex.Message;
            }

            if (why is null)
            {
                read++;
            }
            else
            {
                failed++;
                failures.Add($"{Path.GetRelativePath(args[0], path)}: {why}");
            }

            byVersion[head[3]] = (read, failed);
        }

        foreach (var (version, (read, failed)) in byVersion)
        {
            Console.WriteLine($"version {version}: {read} read, {failed} failed");
        }

        Console.WriteLine($"not models: {notModels}");
        foreach (var line in args.Contains("--all") ? failures : failures.Take(40))
        {
            Console.WriteLine($"  {line}");
        }

        return failures.Count == 0 ? 0 : 1;
    }

    private static string? Implausible(BmdModel model)
    {
        if (model.Name.Any(c => c < ' ' || c > '~'))
        {
            return $"name is not text: {model.Name}";
        }

        foreach (var mesh in model.Meshes)
        {
            if (model.Bones.Length > 0
                && (mesh.Vertices.Any(v => v.BoneIndex < 0 || v.BoneIndex >= model.Bones.Length)
                    || mesh.Normals.Any(n => n.BoneIndex < 0 || n.BoneIndex >= model.Bones.Length)))
            {
                return "a vertex is bound to a bone the model does not have";
            }
        }

        return null;
    }
}
