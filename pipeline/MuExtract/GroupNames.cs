using MuAssets.Models;

namespace MuAssets.Cli;

/// <summary>The name each of a model's meshes is known by, downstream of the .bmd.</summary>
/// <remarks>
/// A mesh is named for the texture it wears, stripped to the basename, and that name is the
/// only record of which faces sample which sheet: the OBJ writes it as a group, the rig
/// writes the bones under it, and clean_lowpoly joins the two back together by it. Three
/// places, one name, and it has to be the same name in all three or the join fails.
///
/// It cannot always be the basename. The Budge Dragon is both meshes on p_d - p_d.jpg is
/// the 128-square body, p_d.tga a 64-square of wings with half of it cut away - two
/// drawings sharing a name. So the extension goes back on, and only on the ones that clash:
/// a basename unique in its model keeps it, which leaves every asset built before this
/// exactly as it was.
///
/// This lives apart from both writers because it is the agreement between them. Written
/// twice it would drift, and it drifted once already: the OBJ started disambiguating while
/// the rig did not, and the Budge Dragon built with no bones at all and said nothing.
/// </remarks>
internal static class GroupNames
{
    /// <summary>One name per mesh, in the model's own mesh order.</summary>
    public static string[] For(BmdMeshGeometry[] geometry)
    {
        var basenames = geometry
            .Select((mesh, index) => string.IsNullOrWhiteSpace(mesh.TextureName)
                ? $"mesh{index}"
                : Path.GetFileNameWithoutExtension(mesh.TextureName))
            .ToArray();

        var clashes = basenames
            .GroupBy(name => name, StringComparer.OrdinalIgnoreCase)
            .Where(one => one.Count() > 1)
            .Select(one => one.Key)
            .ToHashSet(StringComparer.OrdinalIgnoreCase);

        return geometry
            .Select((mesh, index) => clashes.Contains(basenames[index])
                ? mesh.TextureName.Replace('.', '_')
                : basenames[index])
            .ToArray();
    }
}
