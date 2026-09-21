namespace MuAssets.Crypto;

/// <summary>
/// The two trivial ciphers MU Online uses to obfuscate its data files.
/// Neither is cryptographically meaningful; they exist only to stop casual editing.
/// </summary>
public static class MuCrypt
{
    /// <summary>
    /// Rolling XOR-plus-key stream applied to map files (.att, .map, .obj) and to
    /// version-0x0C .bmd payloads. Ported from MapFileDecrypt in ZzzLodTerrain.h.
    /// </summary>
    private static readonly byte[] MapXorKey =
    [
        0xD1, 0x73, 0x52, 0xF6, 0xD2, 0x9A, 0xCB, 0x27,
        0x3E, 0xAF, 0x59, 0x31, 0x37, 0xB3, 0xE7, 0xA2,
    ];

    private const byte MapKeySeed = 0x5E;
    private const byte MapKeyIncrement = 0x3D;

    /// <summary>Three-byte XOR ("bux") layer, applied on top of the map cipher for .att files.</summary>
    private static readonly byte[] BuxCode = [0xFC, 0xCF, 0xAB];

    /// <summary>
    /// Decrypts a map-encrypted buffer. The transform is length-preserving:
    /// each output byte is XORed with the rotating key table, then has the
    /// running key (derived from the *previous ciphertext byte*) subtracted.
    /// </summary>
    public static byte[] MapFileDecrypt(ReadOnlySpan<byte> source)
    {
        var result = new byte[source.Length];
        byte key = MapKeySeed;

        for (var i = 0; i < source.Length; i++)
        {
            var cipher = source[i];
            result[i] = (byte)((cipher ^ MapXorKey[i % 16]) - key);
            key = (byte)(cipher + MapKeyIncrement);
        }

        return result;
    }

    /// <summary>Inverse of <see cref="MapFileDecrypt"/>; the running key chains off the ciphertext it just produced.</summary>
    public static byte[] MapFileEncrypt(ReadOnlySpan<byte> source)
    {
        var result = new byte[source.Length];
        byte key = MapKeySeed;

        for (var i = 0; i < source.Length; i++)
        {
            var cipher = (byte)((source[i] + key) ^ MapXorKey[i % 16]);
            result[i] = cipher;
            key = (byte)(cipher + MapKeyIncrement);
        }

        return result;
    }

    /// <summary>
    /// Applies the three-byte XOR in place. The operation is its own inverse.
    /// </summary>
    public static void BuxConvert(Span<byte> buffer)
    {
        for (var i = 0; i < buffer.Length; i++)
        {
            buffer[i] ^= BuxCode[i % 3];
        }
    }
}
