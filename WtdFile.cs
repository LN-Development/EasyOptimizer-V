using System;
using System.Collections.Generic;
using System.IO;
using System.IO.Compression;
using System.Text;
using CodeWalker.GameFiles;
using CodeWalker.Utils;

namespace EasyOptimizerV
{
    public class WtdFile
    {
        private const uint RSC5_MAGIC = 0x05435352;
        private const uint RESOURCE_TYPE_TEXTURE = 0x08;
        private const uint VIRTUAL_BASE = 0x50000000;
        private const uint PHYSICAL_BASE = 0x60000000;

        public YtdFile AsYtd { get; private set; }
        public string FilePath { get; set; }
        private Dictionary<string, WtdTextureMetadata> savedMetadata = new Dictionary<string, WtdTextureMetadata>(StringComparer.OrdinalIgnoreCase);
        private uint originalResourceType;
        private uint originalVft;
        private uint originalBlockMapPtr;
        private uint originalParentDict;
        private uint originalUsageCount;

        public static WtdFile Load(string filePath)
        {
            byte[] fileData = File.ReadAllBytes(filePath);
            var wtd = new WtdFile();
            wtd.FilePath = filePath;
            wtd.Parse(fileData);
            return wtd;
        }

        private void Parse(byte[] fileData)
        {
            if (fileData.Length < 12)
                throw new InvalidDataException("File too small to be a valid WTD.");

            // Read RSC5 header
            uint magic = BitConverter.ToUInt32(fileData, 0);
            if (magic != RSC5_MAGIC)
                throw new InvalidDataException($"Invalid RSC5 magic: 0x{magic:X8} (expected 0x{RSC5_MAGIC:X8})");

            originalResourceType = BitConverter.ToUInt32(fileData, 4);
            uint flags = BitConverter.ToUInt32(fileData, 8);

            // Decode sizes from flags
            uint virtualSize = (flags & 0x7FFu) << (int)(((flags >> 11) & 0xF) + 8);
            uint physicalSize = ((flags >> 15) & 0x7FFu) << (int)(((flags >> 26) & 0xF) + 8);

            // Decompress zlib data starting at offset 12
            byte[] decompressed;
            using (var compressedStream = new MemoryStream(fileData, 12, fileData.Length - 12))
            using (var zlibStream = new ZLibStream(compressedStream, CompressionMode.Decompress))
            using (var output = new MemoryStream())
            {
                zlibStream.CopyTo(output);
                decompressed = output.ToArray();
            }

            if (decompressed.Length < virtualSize)
                throw new InvalidDataException($"Decompressed data ({decompressed.Length}) smaller than virtual size ({virtualSize}).");

            byte[] virtualData = new byte[virtualSize];
            byte[] physicalData = new byte[Math.Min(physicalSize, (uint)(decompressed.Length - virtualSize))];
            Array.Copy(decompressed, 0, virtualData, 0, virtualData.Length);
            if (physicalData.Length > 0)
                Array.Copy(decompressed, virtualSize, physicalData, 0, physicalData.Length);

            int pos = 0;
            originalVft = ReadU32(virtualData, ref pos);
            originalBlockMapPtr = ReadU32(virtualData, ref pos);
            originalParentDict = ReadU32(virtualData, ref pos);
            originalUsageCount = ReadU32(virtualData, ref pos);

            uint hashTablePtr = ReadU32(virtualData, ref pos);
            ushort hashCount = ReadU16(virtualData, ref pos);
            ushort hashCap = ReadU16(virtualData, ref pos);

            uint texturesPtr = ReadU32(virtualData, ref pos);
            ushort texCount = ReadU16(virtualData, ref pos);
            ushort texCap = ReadU16(virtualData, ref pos);

            // Read hash array
            uint[] hashes = new uint[hashCount];
            if (hashTablePtr != 0)
            {
                int hashOffset = (int)(hashTablePtr - VIRTUAL_BASE);
                for (int i = 0; i < hashCount; i++)
                {
                    hashes[i] = BitConverter.ToUInt32(virtualData, hashOffset + i * 4);
                }
            }

            // Read texture pointer array
            uint[] texPtrs = new uint[texCount];
            if (texturesPtr != 0)
            {
                int ptrOffset = (int)(texturesPtr - VIRTUAL_BASE);
                for (int i = 0; i < texCount; i++)
                {
                    texPtrs[i] = BitConverter.ToUInt32(virtualData, ptrOffset + i * 4);
                }
            }

            // Read each texture struct (80 bytes)
            var textures = new List<Texture>();
            for (int i = 0; i < texCount; i++)
            {
                if (texPtrs[i] == 0) continue;
                int texOffset = (int)(texPtrs[i] - VIRTUAL_BASE);
                if (texOffset < 0 || texOffset + 80 > virtualData.Length) continue;

                var tex = ReadRsc5Texture(virtualData, physicalData, texOffset);
                if (tex != null)
                    textures.Add(tex);
            }

            // Build CodeWalker TextureDictionary and wrap into YtdFile
            var dict = new TextureDictionary();
            dict.BuildFromTextureList(textures);

            AsYtd = new YtdFile();
            AsYtd.TextureDict = dict;
            AsYtd.Name = Path.GetFileName(FilePath ?? "unknown.wtd");
        }

        private Texture ReadRsc5Texture(byte[] virtualData, byte[] physicalData, int offset)
        {
            int pos = offset;

            uint vft = ReadU32(virtualData, ref pos);           // 0
            uint unknown1 = ReadU32(virtualData, ref pos);      // 4
            ushort unknown2 = ReadU16(virtualData, ref pos);    // 8
            ushort unknown3 = ReadU16(virtualData, ref pos);    // 10
            uint unknown4 = ReadU32(virtualData, ref pos);      // 12
            uint unknown5 = ReadU32(virtualData, ref pos);      // 16
            uint namePtr = ReadU32(virtualData, ref pos);       // 20 - FileName pointer
            uint unknown6 = ReadU32(virtualData, ref pos);      // 24
            // Rsc5Texture
            ushort width = ReadU16(virtualData, ref pos);       // 28
            ushort height = ReadU16(virtualData, ref pos);      // 30
            uint formatCode = ReadU32(virtualData, ref pos);    // 32 - Rsc5TextureFormat (D3DFORMAT)
            ushort stride = ReadU16(virtualData, ref pos);      // 36
            byte textureType = virtualData[pos++];              // 38
            byte levels = virtualData[pos++];                   // 39
            float unknown7 = BitConverter.ToSingle(virtualData, pos); pos += 4;  // 40
            float unknown8 = BitConverter.ToSingle(virtualData, pos); pos += 4;  // 44
            float unknown9 = BitConverter.ToSingle(virtualData, pos); pos += 4;  // 48
            float unknown10 = BitConverter.ToSingle(virtualData, pos); pos += 4; // 52
            float unknown11 = BitConverter.ToSingle(virtualData, pos); pos += 4; // 56
            float unknown12 = BitConverter.ToSingle(virtualData, pos); pos += 4; // 60
            uint prevTexOffset = ReadU32(virtualData, ref pos); // 64
            uint nextTexOffset = ReadU32(virtualData, ref pos); // 68
            uint dataPtr = ReadU32(virtualData, ref pos);       // 72 - pointer into physical section
            uint unknown13 = ReadU32(virtualData, ref pos);     // 76

            // Read texture name from virtual data
            string name = "unknown";
            if (namePtr != 0)
            {
                int nameOffset = (int)(namePtr - VIRTUAL_BASE);
                if (nameOffset >= 0 && nameOffset < virtualData.Length)
                {
                    name = ReadNullTermString(virtualData, nameOffset);
                    // Strip common prefixes/suffixes from GTA4 WTD names
                    if (name.StartsWith("pack:/", StringComparison.OrdinalIgnoreCase))
                        name = name.Substring(6);
                    if (name.EndsWith(".dds", StringComparison.OrdinalIgnoreCase))
                        name = name.Substring(0, name.Length - 4);
                }
            }

            // Read pixel data from physical section
            byte[] pixelData = null;
            if (dataPtr != 0)
            {
                int dataOffset = (int)(dataPtr - PHYSICAL_BASE);
                if (dataOffset >= 0 && dataOffset < physicalData.Length)
                {
                    int dataSize = CalculateTextureDataSize((TextureFormat)formatCode, width, height, stride, levels);
                    int available = physicalData.Length - dataOffset;
                    dataSize = Math.Min(dataSize, available);
                    if (dataSize > 0)
                    {
                        pixelData = new byte[dataSize];
                        Array.Copy(physicalData, dataOffset, pixelData, 0, dataSize);
                    }
                }
            }

            // Map format
            TextureFormat format = (TextureFormat)formatCode;

            // Create CodeWalker Texture
            var texture = new Texture();
            texture.Name = name;
            texture.NameHash = JenkHash.GenHash(name.ToLowerInvariant());
            texture.Width = width;
            texture.Height = height;
            texture.Depth = 1;
            texture.Levels = levels > 0 ? levels : (byte)1;
            texture.Format = format;
            texture.Stride = stride;
            texture.Data = new TextureData();
            texture.Data.FullData = pixelData ?? Array.Empty<byte>();

            // Save metadata for round-trip
            savedMetadata[name] = new WtdTextureMetadata
            {
                Vft = vft,
                Unknown1 = unknown1,
                Unknown2 = unknown2,
                Unknown3 = unknown3,
                Unknown4 = unknown4,
                Unknown5 = unknown5,
                Unknown6 = unknown6,
                TextureType = textureType,
                Unknown7 = unknown7,
                Unknown8 = unknown8,
                Unknown9 = unknown9,
                Unknown10 = unknown10,
                Unknown11 = unknown11,
                Unknown12 = unknown12,
                PrevTexOffset = prevTexOffset,
                NextTexOffset = nextTexOffset,
                Unknown13 = unknown13,
                OriginalNameRaw = name
            };

            return texture;
        }

        public byte[] Save()
        {
            var dict = AsYtd?.TextureDict;
            if (dict?.Textures?.data_items == null || dict.Textures.data_items.Length == 0)
                throw new InvalidOperationException("No textures to save.");

            var textures = dict.Textures.data_items;
            int texCount = textures.Length;

            int dictHeaderSize = 32;
            int hashArrayOffset = dictHeaderSize;
            int hashArraySize = texCount * 4;
            int ptrArrayOffset = hashArrayOffset + hashArraySize;
            int ptrArraySize = texCount * 4;
            int texStructsOffset = ptrArrayOffset + ptrArraySize;
            int texStructsSize = texCount * 80;
            int namesOffset = texStructsOffset + texStructsSize;

            var nameBytes = new List<byte[]>();
            var nameOffsets = new List<int>();
            int currentNameOffset = namesOffset;
            for (int i = 0; i < texCount; i++)
            {
                string rawName = textures[i].Name ?? "unknown";
                byte[] nb = Encoding.ASCII.GetBytes(rawName + "\0");
                nameOffsets.Add(currentNameOffset);
                nameBytes.Add(nb);
                currentNameOffset += nb.Length;
            }

            int virtualSize = Align16(currentNameOffset);
            var physicalOffsets = new List<int>();
            int currentPhysOffset = 0;
            var pixelChunks = new List<byte[]>();
            for (int i = 0; i < texCount; i++)
            {
                byte[] pdata = textures[i].Data?.FullData ?? Array.Empty<byte>();
                physicalOffsets.Add(currentPhysOffset);
                pixelChunks.Add(pdata);
                currentPhysOffset += pdata.Length;
            }

            int physicalSize = Align16(currentPhysOffset);

            // Allocate buffers
            byte[] virtualData = new byte[virtualSize];
            byte[] physicalData = new byte[physicalSize];

            // Write physical data
            for (int i = 0; i < texCount; i++)
            {
                Array.Copy(pixelChunks[i], 0, physicalData, physicalOffsets[i], pixelChunks[i].Length);
            }

            uint[] hashes = new uint[texCount];
            for (int i = 0; i < texCount; i++)
                hashes[i] = textures[i].NameHash;

            // Write hash array
            for (int i = 0; i < texCount; i++)
                WriteU32(virtualData, hashArrayOffset + i * 4, hashes[i]);

            // Write texture structs and pointer array
            for (int i = 0; i < texCount; i++)
            {
                uint texStructAddr = VIRTUAL_BASE + (uint)(texStructsOffset + i * 80);
                WriteU32(virtualData, ptrArrayOffset + i * 4, texStructAddr);

                WriteRsc5Texture(virtualData, texStructsOffset + i * 80,
                    textures[i], nameOffsets[i], physicalOffsets[i]);
            }

            // Write name strings
            for (int i = 0; i < texCount; i++)
                Array.Copy(nameBytes[i], 0, virtualData, nameOffsets[i], nameBytes[i].Length);

            // Write dictionary header
            int pos = 0;
            WriteU32(virtualData, pos, originalVft != 0 ? originalVft : 0x00D6F028); pos += 4;
            WriteU32(virtualData, pos, 0); pos += 4; // BlockMapPtr (null)
            WriteU32(virtualData, pos, 0); pos += 4; // ParentDict
            WriteU32(virtualData, pos, originalUsageCount); pos += 4;
            WriteU32(virtualData, pos, VIRTUAL_BASE + (uint)hashArrayOffset); pos += 4;
            WriteU16(virtualData, pos, (ushort)texCount); pos += 2;
            WriteU16(virtualData, pos, (ushort)texCount); pos += 2;
            WriteU32(virtualData, pos, VIRTUAL_BASE + (uint)ptrArrayOffset); pos += 4;
            WriteU16(virtualData, pos, (ushort)texCount); pos += 2;
            WriteU16(virtualData, pos, (ushort)texCount); pos += 2;

            uint paddedVirtual = NextValidRsc5Size((uint)virtualSize);
            uint paddedPhysical = NextValidRsc5Size((uint)physicalSize);

            // Ensure buffers are padded to encodable sizes
            if (paddedVirtual > virtualData.Length)
            {
                byte[] padded = new byte[paddedVirtual];
                Array.Copy(virtualData, padded, virtualData.Length);
                virtualData = padded;
            }
            if (paddedPhysical > physicalData.Length)
            {
                byte[] padded = new byte[paddedPhysical];
                Array.Copy(physicalData, padded, physicalData.Length);
                physicalData = padded;
            }

            uint encodedFlags = EncodeRsc5Flags(paddedVirtual, paddedPhysical);

            byte[] combined = new byte[paddedVirtual + paddedPhysical];
            Array.Copy(virtualData, 0, combined, 0, paddedVirtual);
            Array.Copy(physicalData, 0, combined, paddedVirtual, paddedPhysical);

            byte[] compressed;
            using (var ms = new MemoryStream())
            {
                using (var zlib = new ZLibStream(ms, CompressionLevel.Optimal, leaveOpen: true))
                {
                    zlib.Write(combined, 0, combined.Length);
                }
                compressed = ms.ToArray();
            }

            byte[] output = new byte[12 + compressed.Length];
            WriteU32(output, 0, RSC5_MAGIC);
            WriteU32(output, 4, originalResourceType != 0 ? originalResourceType : RESOURCE_TYPE_TEXTURE);
            WriteU32(output, 8, encodedFlags);
            Array.Copy(compressed, 0, output, 12, compressed.Length);

            return output;
        }

        private void WriteRsc5Texture(byte[] data, int offset, Texture tex, int nameVirtualOffset, int physicalDataOffset)
        {
            string name = tex.Name ?? "unknown";
            WtdTextureMetadata meta = null;
            savedMetadata.TryGetValue(name, out meta);

            int pos = offset;
            // Rsc5TextureBase
            WriteU32(data, pos, meta?.Vft ?? 0x00D50104); pos += 4;    // 0: VFT
            WriteU32(data, pos, meta?.Unknown1 ?? 0); pos += 4;        // 4: Unknown1
            WriteU16(data, pos, meta?.Unknown2 ?? 0); pos += 2;        // 8: Unknown2
            WriteU16(data, pos, meta?.Unknown3 ?? 0); pos += 2;        // 10: Unknown3
            WriteU32(data, pos, meta?.Unknown4 ?? 0); pos += 4;        // 12: Unknown4
            WriteU32(data, pos, meta?.Unknown5 ?? 0); pos += 4;        // 16: Unknown5
            WriteU32(data, pos, VIRTUAL_BASE + (uint)nameVirtualOffset); pos += 4; // 20: NamePtr
            WriteU32(data, pos, meta?.Unknown6 ?? 0); pos += 4;        // 24: Unknown6
            // Rsc5Texture
            WriteU16(data, pos, tex.Width); pos += 2;                   // 28: Width
            WriteU16(data, pos, tex.Height); pos += 2;                  // 30: Height
            WriteU32(data, pos, (uint)tex.Format); pos += 4;            // 32: Format
            WriteU16(data, pos, tex.Stride); pos += 2;                  // 36: Stride
            data[pos++] = meta?.TextureType ?? 0;                       // 38: TextureType
            data[pos++] = tex.Levels;                                   // 39: MipLevels
            WriteSingle(data, pos, meta?.Unknown7 ?? 0f); pos += 4;     // 40: Unknown7
            WriteSingle(data, pos, meta?.Unknown8 ?? 0f); pos += 4;     // 44: Unknown8
            WriteSingle(data, pos, meta?.Unknown9 ?? 0f); pos += 4;     // 48: Unknown9
            WriteSingle(data, pos, meta?.Unknown10 ?? 0f); pos += 4;    // 52: Unknown10
            WriteSingle(data, pos, meta?.Unknown11 ?? 0f); pos += 4;    // 56: Unknown11
            WriteSingle(data, pos, meta?.Unknown12 ?? 0f); pos += 4;    // 60: Unknown12
            WriteU32(data, pos, meta?.PrevTexOffset ?? 0); pos += 4;    // 64: PrevTextureInfoOffset
            WriteU32(data, pos, meta?.NextTexOffset ?? 0); pos += 4;    // 68: NextTextureInfoOffset
            WriteU32(data, pos, PHYSICAL_BASE + (uint)physicalDataOffset); pos += 4; // 72: DataPtr
            WriteU32(data, pos, meta?.Unknown13 ?? 0); pos += 4;        // 76: Unknown13
        }

        private int CalculateTextureDataSize(TextureFormat format, int width, int height, int stride, int levels)
        {
            int total = 0;
            int w = width;
            int h = height;

            for (int mip = 0; mip < Math.Max(1, levels); mip++)
            {
                int mipW = Math.Max(1, w);
                int mipH = Math.Max(1, h);

                if (IsBlockCompressed(format))
                {
                    int bw = Math.Max(1, (mipW + 3) / 4);
                    int bh = Math.Max(1, (mipH + 3) / 4);
                    int blockSize = (format == TextureFormat.D3DFMT_DXT1) ? 8 : 16;
                    total += bw * bh * blockSize;
                }
                else
                {
                    int bpp = GetBytesPerPixel(format);
                    total += mipW * mipH * bpp;
                }

                w /= 2;
                h /= 2;
            }

            return total;
        }

        private bool IsBlockCompressed(TextureFormat format)
        {
            return format == TextureFormat.D3DFMT_DXT1
                || format == TextureFormat.D3DFMT_DXT3
                || format == TextureFormat.D3DFMT_DXT5;
        }

        private int GetBytesPerPixel(TextureFormat format)
        {
            switch (format)
            {
                case TextureFormat.D3DFMT_A8R8G8B8: return 4;
                case TextureFormat.D3DFMT_A8B8G8R8: return 4;
                case TextureFormat.D3DFMT_X8R8G8B8: return 4;
                case TextureFormat.D3DFMT_A1R5G5B5: return 2;
                case TextureFormat.D3DFMT_L8: return 1;
                case TextureFormat.D3DFMT_A8: return 1;
                default: return 4;
            }
        }

        private static uint NextValidRsc5Size(uint size)
        {
            if (size == 0) return 256; // minimum
            // RSC5 sizes must be expressible as base << (shift + 8)
            // where base fits in 11 bits (0-2047) and shift in 4 bits (0-15)
            for (int shift = 0; shift <= 15; shift++)
            {
                uint unit = 1u << (shift + 8);
                uint baseVal = (size + unit - 1) / unit;
                if (baseVal <= 0x7FF)
                    return baseVal * unit;
            }
            return size; // fallback
        }

        private static uint EncodeRsc5Flags(uint virtualSize, uint physicalSize)
        {
            uint vBase = 0, vShift = 0;
            for (int s = 0; s <= 15; s++)
            {
                uint unit = 1u << (s + 8);
                if (virtualSize % unit == 0)
                {
                    uint b = virtualSize / unit;
                    if (b <= 0x7FF)
                    {
                        vBase = b;
                        vShift = (uint)s;
                    }
                }
            }

            uint pBase = 0, pShift = 0;
            for (int s = 0; s <= 15; s++)
            {
                uint unit = 1u << (s + 8);
                if (physicalSize % unit == 0)
                {
                    uint b = physicalSize / unit;
                    if (b <= 0x7FF)
                    {
                        pBase = b;
                        pShift = (uint)s;
                    }
                }
            }

            return (vBase & 0x7FF) | ((vShift & 0xF) << 11) | ((pBase & 0x7FF) << 15) | ((pShift & 0xF) << 26);
        }

        private static uint ReadU32(byte[] data, ref int pos)
        {
            uint val = BitConverter.ToUInt32(data, pos);
            pos += 4;
            return val;
        }

        private static ushort ReadU16(byte[] data, ref int pos)
        {
            ushort val = BitConverter.ToUInt16(data, pos);
            pos += 2;
            return val;
        }

        private static string ReadNullTermString(byte[] data, int offset)
        {
            int end = offset;
            while (end < data.Length && data[end] != 0) end++;
            return Encoding.ASCII.GetString(data, offset, end - offset);
        }

        private static void WriteU32(byte[] data, int offset, uint value)
        {
            data[offset] = (byte)(value & 0xFF);
            data[offset + 1] = (byte)((value >> 8) & 0xFF);
            data[offset + 2] = (byte)((value >> 16) & 0xFF);
            data[offset + 3] = (byte)((value >> 24) & 0xFF);
        }

        private static void WriteU16(byte[] data, int offset, ushort value)
        {
            data[offset] = (byte)(value & 0xFF);
            data[offset + 1] = (byte)((value >> 8) & 0xFF);
        }

        private static void WriteSingle(byte[] data, int offset, float value)
        {
            byte[] bytes = BitConverter.GetBytes(value);
            Array.Copy(bytes, 0, data, offset, 4);
        }

        private static int Align16(int value)
        {
            return (value + 15) & ~15;
        }

        private class WtdTextureMetadata
        {
            public uint Vft;
            public uint Unknown1;
            public ushort Unknown2;
            public ushort Unknown3;
            public uint Unknown4;
            public uint Unknown5;
            public uint Unknown6;
            public byte TextureType;
            public float Unknown7;
            public float Unknown8;
            public float Unknown9;
            public float Unknown10;
            public float Unknown11;
            public float Unknown12;
            public uint PrevTexOffset;
            public uint NextTexOffset;
            public uint Unknown13;
            public string OriginalNameRaw;
        }
    }
}
