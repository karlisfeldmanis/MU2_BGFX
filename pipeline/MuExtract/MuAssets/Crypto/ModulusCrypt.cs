namespace MuAssets.Crypto;

/// <summary>
/// Webzen's "ModulusCryptor", the wrapping around a version-14 .bmd's body in the later
/// seasons' clients (third_party/MuClient_S17_v1.19.74 has 7681 of them).
/// </summary>
/// <remarks>
/// The wrapped body opens with two bytes naming a cipher each (<see cref="BlockCipher.Create"/>)
/// and a 32-byte key of its own, then the data:
/// <list type="number">
/// <item>The first cipher, under a key fixed in the client, unlocks up to three windows
/// of about 1 KB: the middle of the data (only when it is over four windows long), its end,
/// and its start, in that order — the start window covers the file's own key.</item>
/// <item>The second cipher, under that key, decrypts every whole block of the data.</item>
/// </list>
/// What is left is a plain version-10 body.
/// </remarks>
public static class ModulusCrypt
{
    private const int HeaderLength = 34;

    private static readonly byte[] FixedKey = "webzen#@!01webzen#@!01webzen#@!0"u8.ToArray();

    public static byte[] Decrypt(ReadOnlySpan<byte> source)
    {
        if (source.Length < HeaderLength)
        {
            throw new InvalidDataException($"ModulusCryptor body is {source.Length} bytes, shorter than its header.");
        }

        var buffer = source.ToArray();
        var dataSize = buffer.Length - HeaderLength;

        var outer = BlockCipher.Create(buffer[1], FixedKey);
        var window = 1024 - 1024 % outer.BlockSize;
        if (dataSize > 4 * window)
        {
            outer.Decrypt(buffer.AsSpan(2 + dataSize / 2, window));
        }

        if (dataSize > window)
        {
            outer.Decrypt(buffer.AsSpan(buffer.Length - window, window));
            outer.Decrypt(buffer.AsSpan(2, window));
        }

        var inner = BlockCipher.Create(buffer[0], buffer.AsSpan(2, 32));
        inner.Decrypt(buffer.AsSpan(HeaderLength, dataSize - dataSize % inner.BlockSize));

        return buffer[HeaderLength..];
    }
}
