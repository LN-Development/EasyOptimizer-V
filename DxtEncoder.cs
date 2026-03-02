using System;
using System.Drawing;
using System.Runtime.InteropServices;

namespace EasyOptimizerV
{
    public static class DxtEncoder
    {
        public static byte[] CompressDXT1(byte[] rgba, int width, int height)
        {
            return Compress(rgba, width, height, true);
        }

        public static byte[] CompressDXT5(byte[] rgba, int width, int height)
        {
            return Compress(rgba, width, height, false);
        }

        private static byte[] Compress(byte[] rgba, int width, int height, bool dxt1)
        {
            int blockWidth = (width + 3) / 4;
            int blockHeight = (height + 3) / 4;
            int blockSize = dxt1 ? 8 : 16;
            byte[] output = new byte[blockWidth * blockHeight * blockSize];

            int offset = 0;
            for (int y = 0; y < height; y += 4)
            {
                for (int x = 0; x < width; x += 4)
                {
                    byte[] block = Get4x4Block(rgba, width, height, x, y);
                    byte[] compressed = dxt1 ? CompressBlockDXT1(block) : CompressBlockDXT5(block);
                    Array.Copy(compressed, 0, output, offset, blockSize);
                    offset += blockSize;
                }
            }
            return output;
        }

        private static byte[] Get4x4Block(byte[] rgba, int width, int height, int x, int y)
        {
            byte[] block = new byte[16 * 4]; // 16 pixels * 4 bytes (RGBA)
            int idx = 0;
            for (int by = 0; by < 4; by++)
            {
                for (int bx = 0; bx < 4; bx++)
                {
                    int px = x + bx;
                    int py = y + by;
                    if (px >= width) px = width - 1;
                    if (py >= height) py = height - 1;

                    // Assuming BGRA or RGBA input. CodeWalker usually gives BGRA?
                    // Let's assume input is ARGB/BGRA (4 bytes).
                    // We need to standardise to RGBA for processing.
                    // If input is from Bitmap.LockBits(Format32bppArgb), it's BGRA on Little Endian.
                    // So B=0, G=1, R=2, A=3.

                    int inputIdx = (py * width + px) * 4;
                    block[idx + 0] = rgba[inputIdx + 2]; // R
                    block[idx + 1] = rgba[inputIdx + 1]; // G
                    block[idx + 2] = rgba[inputIdx + 0]; // B
                    block[idx + 3] = rgba[inputIdx + 3]; // A
                    idx += 4;
                }
            }
            return block;
        }

        // Trivial DXT1 compressor (min/max bounding box)
        private static byte[] CompressBlockDXT1(byte[] block)
        {
            byte[] output = new byte[8];
            // Find min/max colors
            // Simple bounding box approach
            int minR = 255, minG = 255, minB = 255;
            int maxR = 0, maxG = 0, maxB = 0;

            for (int i = 0; i < 16; i++)
            {
                int r = block[i * 4 + 0];
                int g = block[i * 4 + 1];
                int b = block[i * 4 + 2];
                // int a = block[i * 4 + 3]; // Ignore alpha for DXT1 opaque

                if (r < minR) minR = r;
                if (g < minG) minG = g;
                if (b < minB) minB = b;
                if (r > maxR) maxR = r;
				if (g > maxG) maxG = g;
                if (b > maxB) maxB = b;
            }

            // Pack RGB565
            ushort c0 = Pack565(maxR, maxG, maxB);
            ushort c1 = Pack565(minR, minG, minB);

            if (c0 < c1)
            {
                ushort temp = c0;
                c0 = c1;
                c1 = temp;
            }

            Buffer.BlockCopy(BitConverter.GetBytes(c0), 0, output, 0, 2);
            Buffer.BlockCopy(BitConverter.GetBytes(c1), 0, output, 2, 2);

            // Generate lookup table
            byte[] colorTable = new byte[4 * 3]; // 4 colors, RGB
            Unpack565(c0, out colorTable[0], out colorTable[1], out colorTable[2]);
            Unpack565(c1, out colorTable[3], out colorTable[4], out colorTable[5]);

            // c2 = 2/3 c0 + 1/3 c1
            colorTable[6] = (byte)((2 * colorTable[0] + colorTable[3]) / 3);
            colorTable[7] = (byte)((2 * colorTable[1] + colorTable[4]) / 3);
            colorTable[8] = (byte)((2 * colorTable[2] + colorTable[5]) / 3);

            // c3 = 1/3 c0 + 2/3 c1
            colorTable[9] = (byte)((colorTable[0] + 2 * colorTable[3]) / 3);
            colorTable[10] = (byte)((colorTable[1] + 2 * colorTable[4]) / 3);
            colorTable[11] = (byte)((colorTable[2] + 2 * colorTable[5]) / 3);

            uint indices = 0;
            for (int i = 0; i < 16; i++)
            {
                int r = block[i * 4 + 0];
                int g = block[i * 4 + 1];
                int b = block[i * 4 + 2];

                int bestDist = int.MaxValue;
                int bestIndex = 0;

                for (int k = 0; k < 4; k++)
                {
                    int dr = r - colorTable[k * 3 + 0];
                    int dg = g - colorTable[k * 3 + 1];
                    int db = b - colorTable[k * 3 + 2];
                    int dist = dr * dr + dg * dg + db * db;
                    if (dist < bestDist)
                    {
                        bestDist = dist;
                        bestIndex = k;
                    }
                }
                indices |= (uint)(bestIndex << (2 * i));
            }

            Buffer.BlockCopy(BitConverter.GetBytes(indices), 0, output, 4, 4);
            return output;
        }

        private static byte[] CompressBlockDXT5(byte[] block)
        {
            byte[] output = new byte[16];
            // Alpha block (8 bytes)
            byte minA = 255;
            byte maxA = 0;

            for (int i = 0; i < 16; i++)
            {
                byte a = block[i * 4 + 3];
                if (a < minA) minA = a;
                if (a > maxA) maxA = a;
            }

            output[0] = maxA;
            output[1] = minA;

            // 6 interpolated alpha values
            byte[] alphas = new byte[8];
            alphas[0] = maxA;
            alphas[1] = minA;
            if (maxA > minA)
            {
                for (int i = 1; i < 7; i++)
                    alphas[1 + i] = (byte)(((7 - i) * maxA + i * minA) / 7);
            }
            else
            {
                for (int i = 1; i < 5; i++)
                    alphas[1 + i] = (byte)(((5 - i) * maxA + i * minA) / 5);
                alphas[6] = 0;
                alphas[7] = 255;
            }

            ulong alphaIndices = 0;
            for (int i = 0; i < 16; i++)
            {
                byte a = block[i * 4 + 3];
                int bestDist = int.MaxValue;
                int bestIndex = 0;
                for (int k = 0; k < 8; k++)
                {
                    int dist = Math.Abs(a - alphas[k]);
                    if (dist < bestDist)
                    {
                        bestDist = dist;
                        bestIndex = k;
                    }
                }
                // 3 bits per index
                alphaIndices |= (ulong)bestIndex << (3 * i);
            }

            // Write 6 bytes of indices
            byte[] alphaIndBytes = BitConverter.GetBytes(alphaIndices);
            Buffer.BlockCopy(alphaIndBytes, 0, output, 2, 6);

            // Color block (8 bytes) - same as DXT1 but logic slightly duplicated for independence
            // Re-using CompressBlockDXT1 logic here but without allocating new array
            byte[] colorBlock = CompressBlockDXT1(block);
            Buffer.BlockCopy(colorBlock, 0, output, 8, 8);

            return output;
        }

        private static ushort Pack565(int r, int g, int b)
        {
            return (ushort)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
        }

        private static void Unpack565(ushort c, out byte r, out byte g, out byte b)
        {
            r = (byte)((c >> 11) * 255 / 31);
            g = (byte)(((c >> 5) & 0x3F) * 255 / 63);
            b = (byte)((c & 0x1F) * 255 / 31);
        }
    }
}
