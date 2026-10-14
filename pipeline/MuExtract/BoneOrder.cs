using MuAssets.Models;

namespace MuAssets.Cli;

/// <summary>
/// Reorders a BMD skeleton into the form an engine accepts, and names it unambiguously.
/// </summary>
/// <remarks>
/// The same job the old client's MuBoneOrder does, and it is done here for the same two
/// reasons — BMD guarantees neither of the things a consumer needs.
/// <para>
/// *Parents are not stored first.* Nothing in the format requires a bone's parent to have
/// a lower index, and a hierarchy resolved in storage order will read a parent that has
/// not been computed yet. A depth-first emit fixes it, and the mapping it produces is what
/// lets vertex bone references be remapped alongside.
/// </para>
/// <para>
/// *Names are neither unique nor present.* Ten of a worn part's fifty-six bones have no
/// name at all, and the same is true of the player's. That matters more here than it did
/// in the client: MU2 joins a part to the rig by name at export time, so an empty name is
/// not a cosmetic gap, it is ten bones colliding on one key. An unnamed slot becomes
/// dummy_&lt;sourceIndex&gt;, which is stable across files only where the slot is — which
/// is exactly the honest answer, since two models' dummies are not the same bone.
/// </para>
/// </remarks>
internal sealed class BoneOrder
{
    private BoneOrder(int[] toEngine, int[] toSource, string[] names)
    {
        this.ToEngine = toEngine;
        this.ToSource = toSource;
        this.Names = names;
    }

    /// <summary>Indexed by BMD bone index, yields the engine bone index.</summary>
    public int[] ToEngine { get; }

    /// <summary>Indexed by engine bone index, yields the BMD bone index.</summary>
    public int[] ToSource { get; }

    /// <summary>Unique, non-empty names, indexed by engine bone index.</summary>
    public string[] Names { get; }

    public int Count => this.ToSource.Length;

    /// <summary>The engine-space parent of an engine bone, or -1 for a root.</summary>
    public int ParentOf(BmdModel model, int engineIndex)
    {
        var bone = model.Bones[this.ToSource[engineIndex]];
        if (bone.IsDummy || bone.Parent < 0 || bone.Parent >= model.Bones.Length)
        {
            return -1;
        }

        return this.ToEngine[bone.Parent];
    }

    public static BoneOrder Create(BmdModel model)
    {
        var bones = model.Bones;
        var order = new List<int>(bones.Length);
        var state = new byte[bones.Length];

        for (var i = 0; i < bones.Length; i++)
        {
            Visit(i, bones, state, order);
        }

        var toSource = order.ToArray();
        var toEngine = new int[bones.Length];
        for (var engineIndex = 0; engineIndex < toSource.Length; engineIndex++)
        {
            toEngine[toSource[engineIndex]] = engineIndex;
        }

        return new BoneOrder(toEngine, toSource, UniqueNames(bones, toSource));
    }

    /// <summary>Depth-first emit, so every parent lands before its children.</summary>
    private static void Visit(int index, BmdBone[] bones, byte[] state, List<int> order)
    {
        const byte inProgress = 1;
        const byte done = 2;

        if (state[index] == done)
        {
            return;
        }

        if (state[index] == inProgress)
        {
            throw new InvalidOperationException($"bone hierarchy has a cycle through bone {index}");
        }

        state[index] = inProgress;

        var bone = bones[index];
        if (!bone.IsDummy && bone.Parent >= 0 && bone.Parent < bones.Length)
        {
            Visit(bone.Parent, bones, state, order);
        }

        state[index] = done;
        order.Add(index);
    }

    private static string[] UniqueNames(BmdBone[] bones, int[] toSource)
    {
        var names = new string[toSource.Length];
        var used = new HashSet<string>(StringComparer.Ordinal);

        for (var engineIndex = 0; engineIndex < toSource.Length; engineIndex++)
        {
            var sourceIndex = toSource[engineIndex];
            var bone = bones[sourceIndex];

            var baseName = bone.IsDummy || string.IsNullOrWhiteSpace(bone.Name)
                ? $"dummy_{sourceIndex}"
                : bone.Name.Trim();

            var name = baseName;
            var suffix = 1;
            while (!used.Add(name))
            {
                name = $"{baseName}_{suffix++}";
            }

            names[engineIndex] = name;
        }

        return names;
    }
}
