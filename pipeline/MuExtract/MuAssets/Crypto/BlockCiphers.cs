using System.Buffers.Binary;
using System.Numerics;

namespace MuAssets.Crypto;

/// <summary>
/// The eight block ciphers Webzen's ModulusCryptor chooses between, decryption only.
/// </summary>
/// <remarks>
/// The client was built on Crypto++, so each one is a port of Crypto++ 8.9.0's own
/// (public domain): its byte order, its default key length and round count, and its
/// quirks — MARS decrypts its words in reverse order. 3-Way is the exception: it is
/// Daemen's reference decryption (big-endian, the key through theta and mu), which a
/// round trip against the published vector proved where a reading of Crypto++'s
/// byte-reversed form did not. A cipher handed a longer key than it takes uses
/// the leading bytes, as ModulusCryptor hands every one of them 32.
/// </remarks>
internal abstract class BlockCipher
{
    public abstract int BlockSize { get; }

    protected abstract void DecryptBlock(Span<byte> block);

    /// <summary>Decrypts every whole block of <paramref name="data"/> in place, ECB; a tail is left as it is.</summary>
    public void Decrypt(Span<byte> data)
    {
        for (var at = 0; at + BlockSize <= data.Length; at += BlockSize)
        {
            DecryptBlock(data.Slice(at, BlockSize));
        }
    }

    /// <summary>ModulusCryptor's numbering, the low three bits of its header byte.</summary>
    public static BlockCipher Create(int algorithm, ReadOnlySpan<byte> key) => (algorithm & 7) switch
    {
        0 => new Tea(key),
        1 => new ThreeWay(key),
        2 => new Cast128(key),
        3 => new Rc5(key),
        4 => new Rc6(key),
        5 => new Mars(key),
        6 => new Idea(key),
        _ => new Gost(key),
    };

    /// <summary>Crypto++'s GetUserKey: <paramref name="length"/> bytes into <paramref name="count"/> words, zero beyond.</summary>
    protected static uint[] Words(ReadOnlySpan<byte> key, int length, int count, bool bigEndian)
    {
        var padded = new byte[count * 4];
        key[..Math.Min(length, key.Length)].CopyTo(padded);
        var words = new uint[count];
        for (var i = 0; i < count; i++)
        {
            var bytes = padded.AsSpan(i * 4, 4);
            words[i] = bigEndian ? BinaryPrimitives.ReadUInt32BigEndian(bytes) : BinaryPrimitives.ReadUInt32LittleEndian(bytes);
        }

        return words;
    }

    protected static uint Le(Span<byte> b, int word) => BinaryPrimitives.ReadUInt32LittleEndian(b[(word * 4)..]);
    protected static uint Be(Span<byte> b, int word) => BinaryPrimitives.ReadUInt32BigEndian(b[(word * 4)..]);
    protected static void Le(Span<byte> b, int word, uint v) => BinaryPrimitives.WriteUInt32LittleEndian(b[(word * 4)..], v);
    protected static void Be(Span<byte> b, int word, uint v) => BinaryPrimitives.WriteUInt32BigEndian(b[(word * 4)..], v);
    protected static uint Rotl(uint x, uint n) => BitOperations.RotateLeft(x, (int)(n & 31));
    protected static uint Rotr(uint x, uint n) => BitOperations.RotateRight(x, (int)(n & 31));
}

internal sealed class Tea : BlockCipher
{
    private const uint Delta = 0x9E3779B9;
    private readonly uint[] k;

    public Tea(ReadOnlySpan<byte> key) => k = Words(key, 16, 4, bigEndian: true);

    public override int BlockSize => 8;

    protected override void DecryptBlock(Span<byte> b)
    {
        uint y = Be(b, 0), z = Be(b, 1), sum = unchecked(Delta * 32);
        while (sum != 0)
        {
            z -= ((y << 4) + k[2]) ^ (y + sum) ^ ((y >> 5) + k[3]);
            y -= ((z << 4) + k[0]) ^ (z + sum) ^ ((z >> 5) + k[1]);
            sum -= Delta;
        }

        Be(b, 0, y);
        Be(b, 1, z);
    }
}

internal sealed class ThreeWay : BlockCipher
{
    private const int Rounds = 11;
    private const uint StartD = 0xB1B1;
    private readonly uint[] k;

    public ThreeWay(ReadOnlySpan<byte> key)
    {
        k = Words(key, 12, 3, bigEndian: true);
        Theta(ref k[0], ref k[1], ref k[2]);
        Mu(ref k[0], ref k[1], ref k[2]);
    }

    public override int BlockSize => 12;

    protected override void DecryptBlock(Span<byte> b)
    {
        uint a0 = Be(b, 0), a1 = Be(b, 1), a2 = Be(b, 2), rc = StartD;
        Mu(ref a0, ref a1, ref a2);
        for (var i = 0; i < Rounds; i++)
        {
            a0 ^= k[0] ^ (rc << 16);
            a1 ^= k[1];
            a2 ^= k[2] ^ rc;
            Theta(ref a0, ref a1, ref a2);
            PiGammaPi(ref a0, ref a1, ref a2);
            rc <<= 1;
            if ((rc & 0x10000) != 0)
            {
                rc ^= 0x11011;
            }
        }

        a0 ^= k[0] ^ (rc << 16);
        a1 ^= k[1];
        a2 ^= k[2] ^ rc;
        Theta(ref a0, ref a1, ref a2);
        Mu(ref a0, ref a1, ref a2);
        Be(b, 0, a0);
        Be(b, 1, a1);
        Be(b, 2, a2);
    }

    private static uint ReverseBits(uint v)
    {
        v = ((v >> 1) & 0x55555555) | ((v & 0x55555555) << 1);
        v = ((v >> 2) & 0x33333333) | ((v & 0x33333333) << 2);
        v = ((v >> 4) & 0x0F0F0F0F) | ((v & 0x0F0F0F0F) << 4);
        return BinaryPrimitives.ReverseEndianness(v);
    }

    private static void Mu(ref uint a0, ref uint a1, ref uint a2)
    {
        a1 = ReverseBits(a1);
        var t = ReverseBits(a0);
        a0 = ReverseBits(a2);
        a2 = t;
    }

    private static void Theta(ref uint a0, ref uint a1, ref uint a2)
    {
        var c = a0 ^ a1 ^ a2;
        c = Rotl(c, 16) ^ Rotl(c, 8);
        var b0 = (a0 << 24) ^ (a2 >> 8) ^ (a1 << 8) ^ (a0 >> 24);
        var b1 = (a1 << 24) ^ (a0 >> 8) ^ (a2 << 8) ^ (a1 >> 24);
        a0 ^= c ^ b0;
        a1 ^= c ^ b1;
        a2 ^= c ^ (b0 >> 16) ^ (b1 << 16);
    }

    private static void PiGammaPi(ref uint a0, ref uint a1, ref uint a2)
    {
        var b2 = Rotl(a2, 1);
        var b0 = Rotl(a0, 22);
        a0 = Rotl(b0 ^ (a1 | ~b2), 1);
        a2 = Rotl(b2 ^ (b0 | ~a1), 22);
        a1 ^= b2 | ~b0;
    }
}

internal sealed class Cast128 : BlockCipher
{
    private readonly uint[] k = new uint[32];

    public Cast128(ReadOnlySpan<byte> key)
    {
        var s = CipherTables.Cast;
        var x = Words(key, 16, 4, bigEndian: true);
        var z = new uint[4];
        uint X(int i) => (x[i / 4] >> (8 * (3 - i % 4))) & 0xFF;
        uint Z(int i) => (z[i / 4] >> (8 * (3 - i % 4))) & 0xFF;

        for (var i = 0; i <= 16; i += 16)
        {
            // RFC 2144's key schedule, as Crypto++ copied it.
            z[0] = x[0] ^ s[4][X(0xD)] ^ s[5][X(0xF)] ^ s[6][X(0xC)] ^ s[7][X(0xE)] ^ s[6][X(0x8)];
            z[1] = x[2] ^ s[4][Z(0x0)] ^ s[5][Z(0x2)] ^ s[6][Z(0x1)] ^ s[7][Z(0x3)] ^ s[7][X(0xA)];
            z[2] = x[3] ^ s[4][Z(0x7)] ^ s[5][Z(0x6)] ^ s[6][Z(0x5)] ^ s[7][Z(0x4)] ^ s[4][X(0x9)];
            z[3] = x[1] ^ s[4][Z(0xA)] ^ s[5][Z(0x9)] ^ s[6][Z(0xB)] ^ s[7][Z(0x8)] ^ s[5][X(0xB)];
            k[i + 0] = s[4][Z(0x8)] ^ s[5][Z(0x9)] ^ s[6][Z(0x7)] ^ s[7][Z(0x6)] ^ s[4][Z(0x2)];
            k[i + 1] = s[4][Z(0xA)] ^ s[5][Z(0xB)] ^ s[6][Z(0x5)] ^ s[7][Z(0x4)] ^ s[5][Z(0x6)];
            k[i + 2] = s[4][Z(0xC)] ^ s[5][Z(0xD)] ^ s[6][Z(0x3)] ^ s[7][Z(0x2)] ^ s[6][Z(0x9)];
            k[i + 3] = s[4][Z(0xE)] ^ s[5][Z(0xF)] ^ s[6][Z(0x1)] ^ s[7][Z(0x0)] ^ s[7][Z(0xC)];
            x[0] = z[2] ^ s[4][Z(0x5)] ^ s[5][Z(0x7)] ^ s[6][Z(0x4)] ^ s[7][Z(0x6)] ^ s[6][Z(0x0)];
            x[1] = z[0] ^ s[4][X(0x0)] ^ s[5][X(0x2)] ^ s[6][X(0x1)] ^ s[7][X(0x3)] ^ s[7][Z(0x2)];
            x[2] = z[1] ^ s[4][X(0x7)] ^ s[5][X(0x6)] ^ s[6][X(0x5)] ^ s[7][X(0x4)] ^ s[4][Z(0x1)];
            x[3] = z[3] ^ s[4][X(0xA)] ^ s[5][X(0x9)] ^ s[6][X(0xB)] ^ s[7][X(0x8)] ^ s[5][Z(0x3)];
            k[i + 4] = s[4][X(0x3)] ^ s[5][X(0x2)] ^ s[6][X(0xC)] ^ s[7][X(0xD)] ^ s[4][X(0x8)];
            k[i + 5] = s[4][X(0x1)] ^ s[5][X(0x0)] ^ s[6][X(0xE)] ^ s[7][X(0xF)] ^ s[5][X(0xD)];
            k[i + 6] = s[4][X(0x7)] ^ s[5][X(0x6)] ^ s[6][X(0x8)] ^ s[7][X(0x9)] ^ s[6][X(0x3)];
            k[i + 7] = s[4][X(0x5)] ^ s[5][X(0x4)] ^ s[6][X(0xA)] ^ s[7][X(0xB)] ^ s[7][X(0x7)];
            z[0] = x[0] ^ s[4][X(0xD)] ^ s[5][X(0xF)] ^ s[6][X(0xC)] ^ s[7][X(0xE)] ^ s[6][X(0x8)];
            z[1] = x[2] ^ s[4][Z(0x0)] ^ s[5][Z(0x2)] ^ s[6][Z(0x1)] ^ s[7][Z(0x3)] ^ s[7][X(0xA)];
            z[2] = x[3] ^ s[4][Z(0x7)] ^ s[5][Z(0x6)] ^ s[6][Z(0x5)] ^ s[7][Z(0x4)] ^ s[4][X(0x9)];
            z[3] = x[1] ^ s[4][Z(0xA)] ^ s[5][Z(0x9)] ^ s[6][Z(0xB)] ^ s[7][Z(0x8)] ^ s[5][X(0xB)];
            k[i + 8] = s[4][Z(0x3)] ^ s[5][Z(0x2)] ^ s[6][Z(0xC)] ^ s[7][Z(0xD)] ^ s[4][Z(0x9)];
            k[i + 9] = s[4][Z(0x1)] ^ s[5][Z(0x0)] ^ s[6][Z(0xE)] ^ s[7][Z(0xF)] ^ s[5][Z(0xC)];
            k[i + 10] = s[4][Z(0x7)] ^ s[5][Z(0x6)] ^ s[6][Z(0x8)] ^ s[7][Z(0x9)] ^ s[6][Z(0x2)];
            k[i + 11] = s[4][Z(0x5)] ^ s[5][Z(0x4)] ^ s[6][Z(0xA)] ^ s[7][Z(0xB)] ^ s[7][Z(0x6)];
            x[0] = z[2] ^ s[4][Z(0x5)] ^ s[5][Z(0x7)] ^ s[6][Z(0x4)] ^ s[7][Z(0x6)] ^ s[6][Z(0x0)];
            x[1] = z[0] ^ s[4][X(0x0)] ^ s[5][X(0x2)] ^ s[6][X(0x1)] ^ s[7][X(0x3)] ^ s[7][Z(0x2)];
            x[2] = z[1] ^ s[4][X(0x7)] ^ s[5][X(0x6)] ^ s[6][X(0x5)] ^ s[7][X(0x4)] ^ s[4][Z(0x1)];
            x[3] = z[3] ^ s[4][X(0xA)] ^ s[5][X(0x9)] ^ s[6][X(0xB)] ^ s[7][X(0x8)] ^ s[5][Z(0x3)];
            k[i + 12] = s[4][X(0x8)] ^ s[5][X(0x9)] ^ s[6][X(0x7)] ^ s[7][X(0x6)] ^ s[4][X(0x3)];
            k[i + 13] = s[4][X(0xA)] ^ s[5][X(0xB)] ^ s[6][X(0x5)] ^ s[7][X(0x4)] ^ s[5][X(0x7)];
            k[i + 14] = s[4][X(0xC)] ^ s[5][X(0xD)] ^ s[6][X(0x3)] ^ s[7][X(0x2)] ^ s[6][X(0x8)];
            k[i + 15] = s[4][X(0xE)] ^ s[5][X(0xF)] ^ s[6][X(0x1)] ^ s[7][X(0x0)] ^ s[7][X(0xD)];
        }

        for (var i = 16; i < 32; i++)
        {
            k[i] &= 0x1F;
        }
    }

    public override int BlockSize => 8;

    private static uint F(int kind, uint r, uint km, uint kr)
    {
        var s = CipherTables.Cast;
        var t = Rotl(kind == 1 ? km + r : kind == 2 ? km ^ r : km - r, kr);
        uint a = s[0][t >> 24], b = s[1][(t >> 16) & 0xFF], c = s[2][(t >> 8) & 0xFF], d = s[3][t & 0xFF];
        return kind switch
        {
            1 => ((a ^ b) - c) + d,
            2 => ((a - b) + c) ^ d,
            _ => ((a + b) ^ c) - d,
        };
    }

    protected override void DecryptBlock(Span<byte> b)
    {
        uint r = Be(b, 0), l = Be(b, 1);
        // Sixteen rounds, last first: the kinds run 1, 2, 3 by round number.
        for (var i = 15; i >= 0; i -= 2)
        {
            r ^= F(i % 3 + 1, l, k[i], k[i + 16]);
            l ^= F((i - 1) % 3 + 1, r, k[i - 1], k[i + 15]);
        }

        Be(b, 0, l);
        Be(b, 1, r);
    }
}

internal sealed class Rc5 : BlockCipher
{
    private const int Rounds = 16;
    private readonly uint[] s;

    public Rc5(ReadOnlySpan<byte> key) => s = Rc.Schedule(key, 16, 2 * (Rounds + 1));

    public override int BlockSize => 8;

    protected override void DecryptBlock(Span<byte> b)
    {
        uint a = Le(b, 0), c = Le(b, 1);
        for (var i = Rounds; i >= 1; i--)
        {
            c = Rotr(c - s[2 * i + 1], a) ^ a;
            a = Rotr(a - s[2 * i], c) ^ c;
        }

        Le(b, 0, a - s[0]);
        Le(b, 1, c - s[1]);
    }
}

internal sealed class Rc6 : BlockCipher
{
    private const int Rounds = 20;
    private readonly uint[] s;

    public Rc6(ReadOnlySpan<byte> key) => s = Rc.Schedule(key, 16, 2 * (Rounds + 2));

    public override int BlockSize => 16;

    protected override void DecryptBlock(Span<byte> blk)
    {
        uint a = Le(blk, 0), b = Le(blk, 1), c = Le(blk, 2), d = Le(blk, 3);
        c -= s[2 * Rounds + 3];
        a -= s[2 * Rounds + 2];
        for (var i = Rounds; i >= 1; i--)
        {
            (a, b, c, d) = (d, a, b, c);
            var u = Rotl(d * (2 * d + 1), 5);
            var t = Rotl(b * (2 * b + 1), 5);
            c = Rotr(c - s[2 * i + 1], t) ^ u;
            a = Rotr(a - s[2 * i], u) ^ t;
        }

        Le(blk, 0, a);
        Le(blk, 1, b - s[0]);
        Le(blk, 2, c);
        Le(blk, 3, d - s[1]);
    }
}

/// <summary>The key schedule RC5 and RC6 share.</summary>
internal static class Rc
{
    public static uint[] Schedule(ReadOnlySpan<byte> key, int length, int size)
    {
        var c = Math.Max((length + 3) / 4, 1);
        var l = new uint[c];
        var padded = new byte[c * 4];
        key[..Math.Min(length, key.Length)].CopyTo(padded);
        for (var i = 0; i < c; i++)
        {
            l[i] = BinaryPrimitives.ReadUInt32LittleEndian(padded.AsSpan(i * 4));
        }

        var s = new uint[size];
        s[0] = 0xB7E15163;
        for (var j = 1; j < size; j++)
        {
            s[j] = s[j - 1] + 0x9E3779B9;
        }

        uint a = 0, b = 0;
        for (var h = 0; h < 3 * Math.Max(size, c); h++)
        {
            a = s[h % size] = BitOperations.RotateLeft(s[h % size] + a + b, 3);
            b = l[h % c] = BitOperations.RotateLeft(l[h % c] + a + b, (int)((a + b) & 31));
        }

        return s;
    }
}

internal sealed class Mars : BlockCipher
{
    private readonly uint[] k = new uint[40];

    public Mars(ReadOnlySpan<byte> key)
    {
        const int length = 16;
        var sbox = CipherTables.Mars;
        var t = Words(key, length, 15, bigEndian: false);
        t[length / 4] = length / 4;
        for (var j = 0; j < 4; j++)
        {
            for (var i = 0; i < 15; i++)
            {
                t[i] = t[i] ^ Rotl(t[(i + 8) % 15] ^ t[(i + 13) % 15], 3) ^ (uint)(4 * i + j);
            }

            for (var round = 0; round < 4; round++)
            {
                for (var i = 0; i < 15; i++)
                {
                    t[i] = Rotl(t[i] + sbox[t[(i + 14) % 15] % 512], 9);
                }
            }

            for (var i = 0; i < 10; i++)
            {
                k[10 * j + i] = t[4 * i % 15];
            }
        }

        for (var i = 5; i < 37; i += 2)
        {
            uint w = k[i] | 3;
            uint m = (~w ^ (w << 1)) & (~w ^ (w >> 1)) & 0x7FFFFFFE;
            m &= m >> 1; m &= m >> 2; m &= m >> 4;
            m |= m << 1; m |= m << 2; m |= m << 4;
            m &= 0x7FFFFFFC;
            w ^= Rotl(sbox[265 + (k[i] & 3)], k[i - 1]) & m;
            k[i] = w;
        }
    }

    public override int BlockSize => 16;

    private static uint S(uint x) => CipherTables.Mars[x & 0x1FF];
    private static uint S0(uint x) => CipherTables.Mars[x & 0xFF];
    private static uint S1(uint x) => CipherTables.Mars[(x & 0xFF) + 256];

    protected override void DecryptBlock(Span<byte> blk)
    {
        uint d = Le(blk, 0), c = Le(blk, 1), b = Le(blk, 2), a = Le(blk, 3), t;
        d += k[36]; c += k[37]; b += k[38]; a += k[39];

        for (var i = 0; i < 8; i++)
        {
            b = (b ^ S0(a)) + S1(a >> 8);
            c += S0(a >> 16);
            a = Rotr(a, 24);
            d ^= S1(a);
            a += i % 4 == 0 ? d : 0;
            a += i % 4 == 1 ? b : 0;
            t = a; a = b; b = c; c = d; d = t;
        }

        for (var i = 0; i < 16; i++)
        {
            t = Rotr(a, 13);
            var r = Rotl(a * k[35 - 2 * i], 10);
            var m = t + k[34 - 2 * i];
            var l = Rotl(S(m) ^ Rotr(r, 5) ^ r, r);
            c -= Rotl(m, Rotr(r, 5));
            if (i < 8) { b -= l; d ^= r; } else { d -= l; b ^= r; }
            a = b; b = c; c = d; d = t;
        }

        for (var i = 0; i < 8; i++)
        {
            a -= i % 4 == 2 ? d : 0;
            a -= i % 4 == 3 ? b : 0;
            b ^= S1(a);
            c -= S0(a >> 24);
            t = Rotl(a, 24);
            d = (d - S1(a >> 16)) ^ S0(t);
            a = b; b = c; c = d; d = t;
        }

        Le(blk, 0, d - k[0]);
        Le(blk, 1, c - k[1]);
        Le(blk, 2, b - k[2]);
        Le(blk, 3, a - k[3]);
    }
}

internal sealed class Idea : BlockCipher
{
    private const int Rounds = 8;
    private readonly ushort[] k;

    public Idea(ReadOnlySpan<byte> key)
    {
        var e = new ushort[6 * Rounds + 4];
        for (var i = 0; i < 8; i++)
        {
            e[i] = (ushort)((key[2 * i] << 8) | key[2 * i + 1]);
        }

        for (var i = 8; i < e.Length; i++)
        {
            var j = i / 8 * 8 - 8;
            e[i] = (ushort)((e[j + (i + 1) % 8] << 9) | (e[j + (i + 2) % 8] >> 7));
        }

        k = new ushort[e.Length];
        int r;
        for (r = 0; r < Rounds; r++)
        {
            k[r * 6 + 0] = MulInv(e[(Rounds - r) * 6 + 0]);
            k[r * 6 + 1] = (ushort)-e[(Rounds - r) * 6 + 1 + (r > 0 ? 1 : 0)];
            k[r * 6 + 2] = (ushort)-e[(Rounds - r) * 6 + 2 - (r > 0 ? 1 : 0)];
            k[r * 6 + 3] = MulInv(e[(Rounds - r) * 6 + 3]);
            k[r * 6 + 4] = e[(Rounds - 1 - r) * 6 + 4];
            k[r * 6 + 5] = e[(Rounds - 1 - r) * 6 + 5];
        }

        k[r * 6 + 0] = MulInv(e[0]);
        k[r * 6 + 1] = (ushort)-e[1];
        k[r * 6 + 2] = (ushort)-e[2];
        k[r * 6 + 3] = MulInv(e[3]);
    }

    public override int BlockSize => 8;

    /// <summary>Multiplication modulo 65537, with 0 standing for 65536.</summary>
    private static ushort Mul(ushort a, ushort b)
    {
        var p = (uint)a * b;
        if (p == 0)
        {
            return (ushort)(1 - a - b);
        }

        uint lo = p & 0xFFFF, hi = p >> 16;
        return (ushort)(lo - hi + (lo < hi ? 1u : 0u));
    }

    private static ushort MulInv(ushort x)
    {
        var y = x;
        for (var i = 0; i < 15; i++)
        {
            y = Mul(y, y);
            y = Mul(y, x);
        }

        return y;
    }

    protected override void DecryptBlock(Span<byte> b)
    {
        ushort x0 = BinaryPrimitives.ReadUInt16BigEndian(b);
        ushort x1 = BinaryPrimitives.ReadUInt16BigEndian(b[2..]);
        ushort x2 = BinaryPrimitives.ReadUInt16BigEndian(b[4..]);
        ushort x3 = BinaryPrimitives.ReadUInt16BigEndian(b[6..]);
        for (var i = 0; i < Rounds; i++)
        {
            x0 = Mul(x0, k[i * 6 + 0]);
            x1 += k[i * 6 + 1];
            x2 += k[i * 6 + 2];
            x3 = Mul(x3, k[i * 6 + 3]);
            var t0 = Mul((ushort)(x0 ^ x2), k[i * 6 + 4]);
            var t1 = Mul((ushort)(t0 + (x1 ^ x3)), k[i * 6 + 5]);
            t0 += t1;
            x0 ^= t1;
            x3 ^= t0;
            t0 ^= x1;
            x1 = (ushort)(x2 ^ t1);
            x2 = t0;
        }

        x0 = Mul(x0, k[Rounds * 6 + 0]);
        x2 += k[Rounds * 6 + 1];
        x1 += k[Rounds * 6 + 2];
        x3 = Mul(x3, k[Rounds * 6 + 3]);
        BinaryPrimitives.WriteUInt16BigEndian(b, x0);
        BinaryPrimitives.WriteUInt16BigEndian(b[2..], x2);
        BinaryPrimitives.WriteUInt16BigEndian(b[4..], x1);
        BinaryPrimitives.WriteUInt16BigEndian(b[6..], x3);
    }
}

internal sealed class Gost : BlockCipher
{
    private static readonly byte[,] SBox =
    {
        { 4, 10, 9, 2, 13, 8, 0, 14, 6, 11, 1, 12, 7, 15, 5, 3 },
        { 14, 11, 4, 12, 6, 13, 15, 10, 2, 3, 8, 1, 0, 7, 5, 9 },
        { 5, 8, 1, 13, 10, 3, 4, 2, 14, 15, 12, 7, 6, 0, 9, 11 },
        { 7, 13, 10, 1, 0, 8, 9, 15, 14, 4, 6, 12, 11, 2, 5, 3 },
        { 6, 12, 7, 1, 5, 15, 13, 8, 4, 10, 9, 14, 0, 3, 11, 2 },
        { 4, 11, 10, 0, 7, 2, 1, 13, 3, 6, 8, 5, 9, 12, 15, 14 },
        { 13, 11, 4, 1, 3, 15, 5, 9, 0, 10, 14, 7, 6, 8, 2, 12 },
        { 1, 15, 13, 0, 5, 7, 10, 4, 9, 2, 3, 14, 6, 11, 8, 12 },
    };

    private static readonly uint[,] Table = BuildTable();
    private readonly uint[] k;

    public Gost(ReadOnlySpan<byte> key) => k = Words(key, 32, 8, bigEndian: false);

    public override int BlockSize => 8;

    private static uint[,] BuildTable()
    {
        var table = new uint[4, 256];
        for (var i = 0; i < 4; i++)
        {
            for (var j = 0; j < 256; j++)
            {
                var temp = (uint)(SBox[2 * i, j % 16] | (SBox[2 * i + 1, j / 16] << 4));
                table[i, j] = BitOperations.RotateLeft(temp, 11 + 8 * i);
            }
        }

        return table;
    }

    private static uint F(uint t) =>
        Table[3, t >> 24] ^ Table[2, (t >> 16) & 0xFF] ^ Table[1, (t >> 8) & 0xFF] ^ Table[0, t & 0xFF];

    protected override void DecryptBlock(Span<byte> b)
    {
        uint n1 = Le(b, 0), n2 = Le(b, 1);
        for (var i = 0; i < 8; i += 2)
        {
            n2 ^= F(n1 + k[i]);
            n1 ^= F(n2 + k[i + 1]);
        }

        for (var pass = 0; pass < 3; pass++)
        {
            for (var i = 7; i > 0; i -= 2)
            {
                n2 ^= F(n1 + k[i]);
                n1 ^= F(n2 + k[i - 1]);
            }
        }

        Le(b, 0, n2);
        Le(b, 1, n1);
    }
}
