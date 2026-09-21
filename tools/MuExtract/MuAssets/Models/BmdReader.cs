using System.Buffers.Binary;
using System.Text;
using MuAssets.Crypto;

namespace MuAssets.Models;

/// <summary>
/// Parses .bmd model files.
/// </summary>
/// <remarks>
/// The file opens with the ASCII tag <c>BMD</c> and a version byte. Version 10
/// stores its body in the clear; version 12 stores a 32-bit payload length
/// followed by a map-encrypted body. Version 14 uses a different key and is not
/// handled here.
/// <para>
/// The body is a straight dump of the client's in-memory structs, so records are
/// laid out with C struct padding — see the <c>ByteLength</c> constants on the
/// model types. Faces are the notable trap: the on-disk record is the larger
/// light-mapped struct even though the client only reads its leading fields.
/// </para>
/// </remarks>
public static class BmdReader
{
    private const byte VersionPlain = 0x0A;
    private const byte VersionEncrypted = 0x0C;
    private const byte VersionWebzenEncrypted = 0x0E;

    private const int NameLength = 32;

    public static BmdModel Load(string path)
    {
        try
        {
            return Parse(File.ReadAllBytes(path));
        }
        catch (Exception ex) when (ex is InvalidDataException or NotSupportedException or ArgumentOutOfRangeException)
        {
            throw new InvalidDataException($"Failed to parse '{path}': {ex.Message}", ex);
        }
    }

    public static BmdModel Parse(ReadOnlySpan<byte> file)
    {
        if (file.Length < 4)
        {
            throw new InvalidDataException($"File is {file.Length} bytes, too short to hold a BMD header.");
        }

        if (file[0] != (byte)'B' || file[1] != (byte)'M' || file[2] != (byte)'D')
        {
            throw new InvalidDataException("File is missing its 'BMD' signature.");
        }

        var version = file[3];

        switch (version)
        {
            case VersionPlain:
                return ParseBody(file[4..], version);

            case VersionEncrypted:
            {
                var payloadLength = BinaryPrimitives.ReadInt32LittleEndian(file[4..]);
                if (payloadLength < 0 || 8 + payloadLength > file.Length)
                {
                    throw new InvalidDataException(
                        $"Encrypted payload declares {payloadLength} bytes but only {file.Length - 8} remain.");
                }

                var decrypted = MuCrypt.MapFileDecrypt(file.Slice(8, payloadLength));
                return ParseBody(decrypted, version);
            }

            case VersionWebzenEncrypted:
                throw new NotSupportedException(
                    "BMD version 14 uses a separate encryption key and is not supported yet.");

            default:
                throw new NotSupportedException($"Unknown BMD version {version}.");
        }
    }

    private static BmdModel ParseBody(ReadOnlySpan<byte> body, byte version)
    {
        var cursor = new Cursor(body);

        var name = cursor.ReadFixedString(NameLength);
        var meshCount = cursor.ReadInt16();
        var boneCount = cursor.ReadInt16();
        var actionCount = cursor.ReadInt16();

        if (meshCount < 0 || boneCount < 0 || actionCount < 0)
        {
            throw new InvalidDataException(
                $"Negative counts in header (meshes {meshCount}, bones {boneCount}, actions {actionCount}).");
        }

        var meshes = new BmdMesh[meshCount];
        for (var i = 0; i < meshCount; i++)
        {
            meshes[i] = ReadMesh(ref cursor);
        }

        var actions = new BmdAction[actionCount];
        for (var i = 0; i < actionCount; i++)
        {
            actions[i] = ReadAction(ref cursor);
        }

        var bones = new BmdBone[boneCount];
        for (var i = 0; i < boneCount; i++)
        {
            bones[i] = ReadBone(ref cursor, actions);
        }

        return new BmdModel
        {
            Name = name,
            Version = version,
            Meshes = meshes,
            Bones = bones,
            Actions = actions,
        };
    }

    private static BmdMesh ReadMesh(ref Cursor cursor)
    {
        var vertexCount = cursor.ReadInt16();
        var normalCount = cursor.ReadInt16();
        var texCoordCount = cursor.ReadInt16();
        var triangleCount = cursor.ReadInt16();
        var textureIndex = cursor.ReadInt16();

        if (vertexCount < 0 || normalCount < 0 || texCoordCount < 0 || triangleCount < 0)
        {
            throw new InvalidDataException("Negative element count in mesh header.");
        }

        var vertices = new BmdVertex[vertexCount];
        for (var i = 0; i < vertexCount; i++)
        {
            var record = cursor.Take(BmdVertex.ByteLength);
            vertices[i] = new BmdVertex(
                BinaryPrimitives.ReadInt16LittleEndian(record),
                MuVec3.Read(record[4..]));
        }

        var normals = new BmdNormal[normalCount];
        for (var i = 0; i < normalCount; i++)
        {
            var record = cursor.Take(BmdNormal.ByteLength);
            normals[i] = new BmdNormal(
                BinaryPrimitives.ReadInt16LittleEndian(record),
                MuVec3.Read(record[4..]),
                BinaryPrimitives.ReadInt16LittleEndian(record[16..]));
        }

        var texCoords = new BmdTexCoord[texCoordCount];
        for (var i = 0; i < texCoordCount; i++)
        {
            var record = cursor.Take(BmdTexCoord.ByteLength);
            texCoords[i] = new BmdTexCoord(
                BinaryPrimitives.ReadSingleLittleEndian(record),
                BinaryPrimitives.ReadSingleLittleEndian(record[4..]));
        }

        var triangles = new BmdTriangle[triangleCount];
        for (var i = 0; i < triangleCount; i++)
        {
            triangles[i] = ReadTriangle(cursor.Take(BmdTriangle.ByteLength));
        }

        var textureName = cursor.ReadFixedString(NameLength);

        return new BmdMesh
        {
            TextureIndex = textureIndex,
            TextureName = textureName,
            Vertices = vertices,
            Normals = normals,
            TexCoords = texCoords,
            Triangles = triangles,
        };
    }

    private static BmdTriangle ReadTriangle(ReadOnlySpan<byte> record)
    {
        var vertexIndex = new short[4];
        var normalIndex = new short[4];
        var texCoordIndex = new short[4];
        var lightMapCoord = new BmdTexCoord[4];

        for (var i = 0; i < 4; i++)
        {
            vertexIndex[i] = BinaryPrimitives.ReadInt16LittleEndian(record[(2 + (i * 2))..]);
            normalIndex[i] = BinaryPrimitives.ReadInt16LittleEndian(record[(10 + (i * 2))..]);
            texCoordIndex[i] = BinaryPrimitives.ReadInt16LittleEndian(record[(18 + (i * 2))..]);
            lightMapCoord[i] = new BmdTexCoord(
                BinaryPrimitives.ReadSingleLittleEndian(record[(28 + (i * 8))..]),
                BinaryPrimitives.ReadSingleLittleEndian(record[(32 + (i * 8))..]));
        }

        return new BmdTriangle
        {
            Polygon = record[0],
            VertexIndex = vertexIndex,
            NormalIndex = normalIndex,
            TexCoordIndex = texCoordIndex,
            LightMapCoord = lightMapCoord,
            LightMapIndex = BinaryPrimitives.ReadInt16LittleEndian(record[60..]),
        };
    }

    private static BmdAction ReadAction(ref Cursor cursor)
    {
        var keyCount = cursor.ReadInt16();
        var lockPositions = cursor.ReadByte() != 0;

        if (keyCount < 0)
        {
            throw new InvalidDataException($"Action declares {keyCount} keys.");
        }

        var positions = Array.Empty<MuVec3>();
        if (lockPositions && keyCount > 0)
        {
            positions = cursor.ReadVectors(keyCount);
        }

        return new BmdAction
        {
            KeyCount = keyCount,
            LockPositions = lockPositions,
            Positions = positions,
        };
    }

    private static BmdBone ReadBone(ref Cursor cursor, BmdAction[] actions)
    {
        // A non-zero marker means the slot is a placeholder with no further data.
        if (cursor.ReadByte() != 0)
        {
            return new BmdBone { IsDummy = true, Parent = -1 };
        }

        var name = cursor.ReadFixedString(NameLength);
        var parent = cursor.ReadInt16();

        var tracks = new BmdBoneTrack[actions.Length];
        for (var i = 0; i < actions.Length; i++)
        {
            var keyCount = actions[i].KeyCount;
            tracks[i] = keyCount > 0
                ? new BmdBoneTrack
                {
                    Positions = cursor.ReadVectors(keyCount),
                    Rotations = cursor.ReadVectors(keyCount),
                }
                : new BmdBoneTrack();
        }

        return new BmdBone
        {
            IsDummy = false,
            Name = name,
            Parent = parent,
            Tracks = tracks,
        };
    }

    /// <summary>A forward-only cursor that fails loudly on overrun, so layout mistakes surface immediately.</summary>
    private ref struct Cursor(ReadOnlySpan<byte> data)
    {
        private readonly ReadOnlySpan<byte> data = data;
        private int offset = 0;

        public ReadOnlySpan<byte> Take(int count)
        {
            if (this.offset + count > this.data.Length)
            {
                throw new InvalidDataException(
                    $"Read of {count} bytes at offset {this.offset} runs past the end of the {this.data.Length} byte body.");
            }

            var slice = this.data.Slice(this.offset, count);
            this.offset += count;
            return slice;
        }

        public byte ReadByte() => this.Take(1)[0];

        public short ReadInt16() => BinaryPrimitives.ReadInt16LittleEndian(this.Take(sizeof(short)));

        public MuVec3[] ReadVectors(int count)
        {
            var result = new MuVec3[count];
            var block = this.Take(count * MuVec3.ByteLength);

            for (var i = 0; i < count; i++)
            {
                result[i] = MuVec3.Read(block[(i * MuVec3.ByteLength)..]);
            }

            return result;
        }

        /// <summary>Reads a fixed-width, null-padded field and trims it to the real string.</summary>
        public string ReadFixedString(int length)
        {
            var raw = this.Take(length);
            var end = raw.IndexOf((byte)0);
            var text = end >= 0 ? raw[..end] : raw;
            return Encoding.ASCII.GetString(text).Trim();
        }
    }
}
