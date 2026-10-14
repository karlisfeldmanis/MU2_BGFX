using MuAssets.Numerics;

namespace MuAssets.Models;

/// <summary>
/// Resolves a BMD skeleton into world-space bone transforms.
/// </summary>
/// <remarks>
/// Each bone stores, per action, a translation and an Euler rotation for every
/// animation key. A bone's global transform is its parent's global transform
/// composed with its own local transform; dummy bones resolve to identity.
/// </remarks>
public static class BmdPose
{
    /// <summary>The action and key that define the bind pose used for mesh building.</summary>
    public const int RestAction = 0;

    /// <summary>The key within <see cref="RestAction"/> that defines the bind pose.</summary>
    public const int RestKey = 0;

    /// <summary>Reads a bone's local transform at an exact animation key.</summary>
    public static MuTransform LocalAt(BmdBone bone, int action, int key)
    {
        if (bone.IsDummy || action < 0 || action >= bone.Tracks.Length)
        {
            return MuTransform.Identity;
        }

        var track = bone.Tracks[action];
        if (track.Positions.Length == 0 || key < 0 || key >= track.Positions.Length)
        {
            return MuTransform.Identity;
        }

        return new MuTransform(
            MuQuaternion.FromEulerRadians(track.Rotations[key]),
            track.Positions[key]);
    }

    /// <summary>
    /// Reads a bone's local transform at a fractional frame, blending between keys
    /// the way the client does: NLERP for rotation, linear for translation.
    /// </summary>
    public static MuTransform LocalAtFrame(BmdBone bone, int action, float frame, bool loop = true)
    {
        if (bone.IsDummy || action < 0 || action >= bone.Tracks.Length)
        {
            return MuTransform.Identity;
        }

        var track = bone.Tracks[action];
        var keyCount = track.Positions.Length;
        if (keyCount == 0)
        {
            return MuTransform.Identity;
        }

        if (keyCount == 1)
        {
            return LocalAt(bone, action, 0);
        }

        var clamped = Math.Max(frame, 0f);
        var keyIndex = (int)clamped;
        var blend = clamped - keyIndex;

        keyIndex = loop ? keyIndex % keyCount : Math.Min(keyIndex, keyCount - 1);
        var nextIndex = loop ? (keyIndex + 1) % keyCount : Math.Min(keyIndex + 1, keyCount - 1);

        return new MuTransform(
            MuQuaternion.Nlerp(
                MuQuaternion.FromEulerRadians(track.Rotations[keyIndex]),
                MuQuaternion.FromEulerRadians(track.Rotations[nextIndex]),
                blend),
            MuVec3.Lerp(track.Positions[keyIndex], track.Positions[nextIndex], blend));
    }

    /// <summary>
    /// Composes every bone's global transform for one exact key.
    /// </summary>
    public static MuTransform[] ResolveGlobal(BmdModel model, int action, int key)
        => Resolve(model, bone => LocalAt(bone, action, key));

    /// <summary>Composes every bone's global transform at a fractional frame.</summary>
    public static MuTransform[] ResolveGlobalAtFrame(BmdModel model, int action, float frame, bool loop = true)
        => Resolve(model, bone => LocalAtFrame(bone, action, frame, loop));

    /// <summary>The bind pose that mesh vertices are baked against.</summary>
    public static MuTransform[] RestPose(BmdModel model) => ResolveGlobal(model, RestAction, RestKey);

    private static MuTransform[] Resolve(BmdModel model, Func<BmdBone, MuTransform> localOf)
    {
        var bones = model.Bones;
        var global = new MuTransform[bones.Length];
        var state = new ResolutionState[bones.Length];

        for (var i = 0; i < bones.Length; i++)
        {
            ResolveBone(i, bones, global, state, localOf);
        }

        return global;
    }

    private static void ResolveBone(
        int index,
        BmdBone[] bones,
        MuTransform[] global,
        ResolutionState[] state,
        Func<BmdBone, MuTransform> localOf)
    {
        if (state[index] == ResolutionState.Done)
        {
            return;
        }

        if (state[index] == ResolutionState.InProgress)
        {
            // A parent cycle would otherwise recurse until the stack gives out.
            throw new InvalidDataException($"Bone hierarchy contains a cycle through bone {index}.");
        }

        state[index] = ResolutionState.InProgress;

        var bone = bones[index];
        var local = localOf(bone);
        var parent = bone.Parent;

        if (bone.IsDummy)
        {
            // Dummy slots hold no data; identity keeps any child or vertex that
            // references them anchored rather than inheriting stale values.
            global[index] = MuTransform.Identity;
        }
        else if (parent < 0)
        {
            global[index] = local;
        }
        else if (parent >= bones.Length)
        {
            throw new InvalidDataException(
                $"Bone {index} names parent {parent}, outside the {bones.Length} bone skeleton.");
        }
        else
        {
            // Parents normally precede their children, but resolve on demand so an
            // out-of-order skeleton still composes correctly.
            ResolveBone(parent, bones, global, state, localOf);
            global[index] = MuTransform.Concat(global[parent], local);
        }

        state[index] = ResolutionState.Done;
    }

    private enum ResolutionState : byte
    {
        Pending = 0,
        InProgress = 1,
        Done = 2,
    }
}
