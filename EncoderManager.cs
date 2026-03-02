using System;
using System.IO;
using System.Drawing;
using System.Drawing.Imaging;
using System.Diagnostics;
using System.Collections.Generic;
using BCnEncoder.Encoder;
using BCnEncoder.Shared;
using CodeWalker.GameFiles;

namespace EasyOptimizerV
{
    public enum EncoderEngine
    {
        BCnEncoder,
        NVTT,
        DirectXTex,
        ImageMagick
    }

    public interface ITextureEncoder
    {
        byte[] Encode(Bitmap bitmap, TextureFormat targetFormat, int mipLimit, out int mipsGenerated);
    }

    public static class EncoderManager
    {
        public static ITextureEncoder GetEncoder(EncoderEngine engine)
        {
            switch (engine)
            {
                case EncoderEngine.BCnEncoder:
                    return new BCnEncoderAdapter();
                case EncoderEngine.NVTT:
                    return new NvttEncoderAdapter();
                case EncoderEngine.DirectXTex:
                    return new DirectXTexEncoderAdapter();
                case EncoderEngine.ImageMagick:
                    return new MagickEncoderAdapter();
                default:
                    return new BCnEncoderAdapter();
            }
        }

        public static void RunCli(string exeName, string args) {
            string exePath = ResolveExePath(exeName);
            var info = new ProcessStartInfo(exePath, args) {
                UseShellExecute = false,
                CreateNoWindow = true,
                RedirectStandardError = true,
                RedirectStandardOutput = true
            };
            try {
                using (var proc = Process.Start(info)) {
                    proc.WaitForExit();
                    if (proc.ExitCode != 0) {
                        string err = proc.StandardError.ReadToEnd();
                        throw new Exception($"{exeName} failed with exit code {proc.ExitCode}: {err}");
                    }
                }
            } catch (System.ComponentModel.Win32Exception) {
                throw new Exception($"Could not find '{exeName}.exe'. Please ensure it is in the application folder, in the 'libs' folder, or in your system's PATH.");
            }
        }

        public static string ResolveExePath(string exeName) {
            string localName = exeName.EndsWith(".exe") ? exeName : exeName + ".exe";
            
            // 1. Check application folder
            string appPath = Path.Combine(AppDomain.CurrentDomain.BaseDirectory, localName);
            if (File.Exists(appPath)) return appPath;
            
            // 2. Check libs folder relative to app
            string libsRelPath = Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "libs", localName);
            if (File.Exists(libsRelPath)) return libsRelPath;
            
            // 3. Fallback to global PATH
            return exeName;
        }
    }

    public class BCnEncoderAdapter : ITextureEncoder
    {
        public byte[] Encode(Bitmap bitmap, TextureFormat targetFormat, int mipLimit, out int mipsGenerated)
        {
            if (!Mapping.IsCompressed(targetFormat))
            {
                return EncodeUncompressed(bitmap, targetFormat, mipLimit, out mipsGenerated);
            }

            mipsGenerated = 0;
            CompressionFormat bcFormat = Mapping.ToBCnFormat(targetFormat);
            
            var encoder = new BcEncoder();
            encoder.OutputOptions.GenerateMipMaps = false;
            encoder.OutputOptions.Quality = CompressionQuality.Balanced;
            encoder.OutputOptions.Format = bcFormat;

            var mipDataList = new List<byte[]>();
            int currentW = bitmap.Width;
            int currentH = bitmap.Height;

            while (currentW >= 1 || currentH >= 1)
            {
                if (mipsGenerated >= mipLimit) break;

                int w = Math.Max(1, currentW);
                int h = Math.Max(1, currentH);

                using (Bitmap mipBmp = new Bitmap(w, h, System.Drawing.Imaging.PixelFormat.Format32bppArgb))
                {
                    using (Graphics g = Graphics.FromImage(mipBmp))
                    {
                        g.InterpolationMode = System.Drawing.Drawing2D.InterpolationMode.HighQualityBicubic;
                        g.DrawImage(bitmap, 0, 0, w, h);
                    }

                    var mipData = mipBmp.LockBits(new Rectangle(0, 0, w, h), System.Drawing.Imaging.ImageLockMode.ReadOnly, System.Drawing.Imaging.PixelFormat.Format32bppArgb);
                    byte[] rawBytes = new byte[w * h * 4];
                    for (int y = 0; y < h; y++)
                    {
                        IntPtr srcRow = mipData.Scan0 + (y * mipData.Stride);
                        System.Runtime.InteropServices.Marshal.Copy(srcRow, rawBytes, y * w * 4, w * 4);
                    }
                    mipBmp.UnlockBits(mipData);

                    // BGRA to RGBA for BCnEncoder
                    for (int i = 0; i < rawBytes.Length; i += 4)
                    {
                        byte b = rawBytes[i];
                        rawBytes[i] = rawBytes[i + 2];
                        rawBytes[i + 2] = b;
                    }

                    byte[] compressedBytes = encoder.EncodeToRawBytes(rawBytes, w, h, BCnEncoder.Encoder.PixelFormat.Rgba32)[0];
                    mipDataList.Add(compressedBytes);
                }

                if (currentW == 1 && currentH == 1) break;
                currentW /= 2;
                currentH /= 2;
                mipsGenerated++;
            }

            int totalSize = 0;
            foreach (var md in mipDataList) totalSize += md.Length;
            byte[] fullData = new byte[totalSize];
            int offset = 0;
            foreach (var md in mipDataList)
            {
                Buffer.BlockCopy(md, 0, fullData, offset, md.Length);
                offset += md.Length;
            }
            return fullData;
        }

        private byte[] EncodeUncompressed(Bitmap bitmap, TextureFormat targetFormat, int mipLimit, out int mipsGenerated)
        {
            mipsGenerated = 0;
            var mipDataList = new List<byte[]>();
            int currentW = bitmap.Width;
            int currentH = bitmap.Height;

            while (currentW >= 1 || currentH >= 1)
            {
                if (mipsGenerated >= mipLimit) break;
                int w = Math.Max(1, currentW);
                int h = Math.Max(1, currentH);

                using (Bitmap mipBmp = new Bitmap(w, h, System.Drawing.Imaging.PixelFormat.Format32bppArgb))
                {
                    using (Graphics g = Graphics.FromImage(mipBmp))
                    {
                        g.InterpolationMode = System.Drawing.Drawing2D.InterpolationMode.HighQualityBicubic;
                        g.DrawImage(bitmap, 0, 0, w, h);
                    }
                    
                    byte[] rawData = Mapping.GetUncompressedRawBytes(mipBmp, targetFormat);
                    mipDataList.Add(rawData);
                }

                if (currentW == 1 && currentH == 1) break;
                currentW /= 2;
                currentH /= 2;
                mipsGenerated++;
            }

            int totalSize = 0;
            foreach (var md in mipDataList) totalSize += md.Length;
            byte[] fullData = new byte[totalSize];
            int offset = 0;
            foreach (var md in mipDataList)
            {
                Buffer.BlockCopy(md, 0, fullData, offset, md.Length);
                offset += md.Length;
            }
            return fullData;
        }
    }

    public class NvttEncoderAdapter : ITextureEncoder
    {
        static NvttEncoderAdapter() {
            string path = Environment.GetEnvironmentVariable("PATH") ?? "";
            string binPath = AppDomain.CurrentDomain.BaseDirectory;
            if (!path.Contains(binPath)) {
                Environment.SetEnvironmentVariable("PATH", binPath + Path.PathSeparator + path);
            }
        }

        public byte[] Encode(Bitmap bitmap, TextureFormat targetFormat, int mipLimit, out int mipsGenerated)
        {
            Exception teximpEx = null;
            try {
                return EncodeWithTeximp(bitmap, targetFormat, mipLimit, out mipsGenerated);
            } catch (Exception ex) {
                teximpEx = ex;
                System.Diagnostics.Debug.WriteLine("TeximpNet failed: " + ex.ToString());
            }

            try {
                return EncodeWithCli(bitmap, targetFormat, mipLimit, out mipsGenerated);
            } catch (Exception cliEx) {
                string msg = $"NVTT failed via both DLL and CLI.\n\nDLL Error: {teximpEx?.Message}\n\nCLI Error: {cliEx.Message}";
                throw new Exception(msg);
            }
        }

        private byte[] EncodeWithTeximp(Bitmap bitmap, TextureFormat targetFormat, int mipLimit, out int mipsGenerated) {
            string tempVal = Path.Combine(Path.GetTempPath(), $"teximp_{Guid.NewGuid()}.png");
            string tempDds = Path.Combine(Path.GetTempPath(), $"teximp_{Guid.NewGuid()}.dds");
            
            try {
                bitmap.Save(tempVal, System.Drawing.Imaging.ImageFormat.Png);
                using (var surface = TeximpNet.Surface.LoadFromFile(tempVal, true))
                using (var compressor = new TeximpNet.Compression.Compressor()) {
                    compressor.Input.SetData(surface);
                    compressor.Compression.Format = Mapping.ToTeximpFormat(targetFormat);
                    
                    int mipLevels = mipLimit > 1 ? mipLimit : 1;
                    if (mipLimit == 0 || mipLimit == 99) mipLevels = 10; // Max

                    compressor.Input.SetMipmapGeneration(mipLimit > 1, mipLevels);
                    compressor.Process(tempDds);
                }
                
                byte[] ddsData = File.ReadAllBytes(tempDds);
                return ParseDds(ddsData, out mipsGenerated);
            } finally {
                if (File.Exists(tempVal)) File.Delete(tempVal);
                if (File.Exists(tempDds)) File.Delete(tempDds);
            }
        }

        private byte[] EncodeWithCli(Bitmap bitmap, TextureFormat targetFormat, int mipLimit, out int mipsGenerated) {
            string format = Mapping.ToNvttFormat(targetFormat);
            string tempIn = Path.Combine(Path.GetTempPath(), $"nvtt_in_{Guid.NewGuid()}.png");
            string tempOut = Path.Combine(Path.GetTempPath(), $"nvtt_out_{Guid.NewGuid()}.dds");
            
            try {
                bitmap.Save(tempIn, ImageFormat.Png);
                string mipsArg = mipLimit > 1 ? "--mips" : "";
                EncoderManager.RunCli("nvtt_export", $"{mipsArg} --format {format} --output \"{tempOut}\" \"{tempIn}\"");
                
                byte[] ddsData = File.ReadAllBytes(tempOut);
                return ParseDds(ddsData, out mipsGenerated);
            } finally {
                if (File.Exists(tempIn)) File.Delete(tempIn);
                if (File.Exists(tempOut)) File.Delete(tempOut);
            }
        }



        private byte[] ParseDds(byte[] ddsData, out int mipsGenerated) {
            mipsGenerated = ddsData[28];
            if (mipsGenerated == 0) mipsGenerated = 1;
            byte[] pixelData = new byte[ddsData.Length - 128];
            Buffer.BlockCopy(ddsData, 128, pixelData, 0, pixelData.Length);
            return pixelData;
        }
    }

    public class DirectXTexEncoderAdapter : ITextureEncoder
    {
        public byte[] Encode(Bitmap bitmap, TextureFormat targetFormat, int mipLimit, out int mipsGenerated)
        {
            string format = Mapping.ToTexconvFormat(targetFormat);
            string tempIn = Path.Combine(Path.GetTempPath(), $"tex_in_{Guid.NewGuid()}.png");
            string tempOutDir = Path.Combine(Path.GetTempPath(), Guid.NewGuid().ToString());
            Directory.CreateDirectory(tempOutDir);
            
            try {
                bitmap.Save(tempIn, ImageFormat.Png);
                int mips = mipLimit > 0 ? (mipLimit == 99 ? 0 : mipLimit) : 1; 
                string args = $"-f {format} -m {mips} -o \"{tempOutDir}\" -y \"{tempIn}\"";
                
                EncoderManager.RunCli("texconv", args);
                
                string outFile = Path.Combine(tempOutDir, Path.GetFileNameWithoutExtension(tempIn) + ".dds");
                byte[] ddsData = File.ReadAllBytes(outFile);
                return ParseDds(ddsData, out mipsGenerated);
            } finally {
                if (File.Exists(tempIn)) File.Delete(tempIn);
                if (Directory.Exists(tempOutDir)) Directory.Delete(tempOutDir, true);
            }
        }



        private byte[] ParseDds(byte[] ddsData, out int mipsGenerated) {
            mipsGenerated = ddsData[28];
            byte[] pixelData = new byte[ddsData.Length - 128];
            Buffer.BlockCopy(ddsData, 128, pixelData, 0, pixelData.Length);
            return pixelData;
        }
    }

    public class MagickEncoderAdapter : ITextureEncoder
    {
        public byte[] Encode(Bitmap bitmap, TextureFormat targetFormat, int mipLimit, out int mipsGenerated)
        {
            using (var image = new ImageMagick.MagickImage()) {
                using (var ms = new MemoryStream()) {
                    bitmap.Save(ms, ImageFormat.Png);
                    ms.Position = 0;
                    image.Read(ms);
                }

                image.Settings.SetDefine(ImageMagick.MagickFormat.Dds, "compression", Mapping.ToMagickDdsCompression(targetFormat));
                
                if (mipLimit > 1) {
                    image.Settings.SetDefine(ImageMagick.MagickFormat.Dds, "mipmaps", mipLimit.ToString());
                } else if (mipLimit == 0 || mipLimit == 99) {
                    // Magick.NET uses 0 for no mipmaps, but CLI/CodeWalker might mean differently.
                    // Usually 0 or high number means "as many as possible".
                    image.Settings.SetDefine(ImageMagick.MagickFormat.Dds, "mipmaps", "max");
                }

                using (var outMs = new MemoryStream()) {
                    image.Write(outMs, ImageMagick.MagickFormat.Dds);
                    byte[] ddsData = outMs.ToArray();
                    return ParseDds(ddsData, out mipsGenerated);
                }
            }
        }

        private byte[] ParseDds(byte[] ddsData, out int mipsGenerated) {
            mipsGenerated = ddsData[28];
            if (mipsGenerated == 0) mipsGenerated = 1;
            byte[] pixelData = new byte[ddsData.Length - 128];
            Buffer.BlockCopy(ddsData, 128, pixelData, 0, pixelData.Length);
            return pixelData;
        }
    }

    public static class Mapping
    {
        public static CompressionFormat ToBCnFormat(TextureFormat format)
        {
            switch (format)
            {
                case TextureFormat.D3DFMT_DXT1: return CompressionFormat.Bc1;
                case TextureFormat.D3DFMT_DXT3: return CompressionFormat.Bc2;
                case TextureFormat.D3DFMT_DXT5: return CompressionFormat.Bc3;
                case TextureFormat.D3DFMT_ATI1: return CompressionFormat.Bc4;
                case TextureFormat.D3DFMT_ATI2: return CompressionFormat.Bc5;
                case TextureFormat.D3DFMT_BC7: return CompressionFormat.Bc7;
                default: return CompressionFormat.Bc3;
            }
        }

        public static TeximpNet.Compression.CompressionFormat ToTeximpFormat(TextureFormat format) {
            switch (format) {
                case TextureFormat.D3DFMT_DXT1: return TeximpNet.Compression.CompressionFormat.BC1;
                case TextureFormat.D3DFMT_DXT3: return TeximpNet.Compression.CompressionFormat.BC2;
                case TextureFormat.D3DFMT_DXT5: return TeximpNet.Compression.CompressionFormat.BC3;
                case TextureFormat.D3DFMT_ATI1: return TeximpNet.Compression.CompressionFormat.BC4;
                case TextureFormat.D3DFMT_ATI2: return TeximpNet.Compression.CompressionFormat.BC5;
                case TextureFormat.D3DFMT_BC7: return TeximpNet.Compression.CompressionFormat.BC7;
                case TextureFormat.D3DFMT_A8R8G8B8: return TeximpNet.Compression.CompressionFormat.BGRA;
                default: return TeximpNet.Compression.CompressionFormat.BC3;
            }
        }

        public static string ToNvttFormat(TextureFormat format) {
            switch (format) {
                case TextureFormat.D3DFMT_DXT1: return "dxt1";
                case TextureFormat.D3DFMT_DXT3: return "dxt3";
                case TextureFormat.D3DFMT_DXT5: return "dxt5";
                case TextureFormat.D3DFMT_ATI1: return "bc4";
                case TextureFormat.D3DFMT_ATI2: return "bc5";
                case TextureFormat.D3DFMT_BC7: return "bc7";
                default: return "dxt5";
            }
        }

        public static string ToTexconvFormat(TextureFormat format) {
            switch (format) {
                case TextureFormat.D3DFMT_DXT1: return "DXT1";
                case TextureFormat.D3DFMT_DXT3: return "DXT3";
                case TextureFormat.D3DFMT_DXT5: return "DXT5";
                case TextureFormat.D3DFMT_ATI1: return "BC4_UNORM";
                case TextureFormat.D3DFMT_ATI2: return "BC5_UNORM";
                case TextureFormat.D3DFMT_BC7: return "BC7_UNORM";
                case TextureFormat.D3DFMT_A8R8G8B8: return "R8G8B8A8_UNORM";
                case TextureFormat.D3DFMT_A1R5G5B5: return "B5G5R5A1_UNORM";
                case TextureFormat.D3DFMT_A8: return "A8_UNORM";
                default: return "DXT5";
            }
        }

        public static string ToMagickDdsCompression(TextureFormat format) {
            switch (format) {
                case TextureFormat.D3DFMT_DXT1: return "dxt1";
                case TextureFormat.D3DFMT_DXT3: return "dxt3";
                case TextureFormat.D3DFMT_DXT5: return "dxt5";
                case TextureFormat.D3DFMT_ATI1: return "bc4";
                case TextureFormat.D3DFMT_ATI2: return "bc5";
                case TextureFormat.D3DFMT_BC7: return "bc7";
                default: return "none";
            }
        }

        public static string ToMagickFormat(TextureFormat format) {
            switch (format) {
                case TextureFormat.D3DFMT_DXT1: return "dxt1";
                case TextureFormat.D3DFMT_DXT3: return "dxt3";
                case TextureFormat.D3DFMT_DXT5: return "dxt5";
                default: return "dxt5";
            }
        }

        public static TextureFormat ParseFormat(string selection, TextureFormat original) {
            if (selection.StartsWith("Preserve")) return original;
            if (selection.Contains("DXT1")) return TextureFormat.D3DFMT_DXT1;
            if (selection.Contains("DXT3")) return TextureFormat.D3DFMT_DXT3;
            if (selection.Contains("DXT5")) return TextureFormat.D3DFMT_DXT5;
            if (selection.Contains("ATI1")) return TextureFormat.D3DFMT_ATI1;
            if (selection.Contains("ATI2")) return TextureFormat.D3DFMT_ATI2;
            if (selection.Contains("BC7")) return TextureFormat.D3DFMT_BC7;
            if (selection.Contains("A1R5G5B5")) return TextureFormat.D3DFMT_A1R5G5B5;
            if (selection.Contains("A8")) return TextureFormat.D3DFMT_A8;
            return TextureFormat.D3DFMT_A8R8G8B8;
        }

        public static bool IsCompressed(TextureFormat format) {
            switch (format) {
                case TextureFormat.D3DFMT_DXT1:
                case TextureFormat.D3DFMT_DXT3:
                case TextureFormat.D3DFMT_DXT5:
                case TextureFormat.D3DFMT_ATI1:
                case TextureFormat.D3DFMT_ATI2:
                case TextureFormat.D3DFMT_BC7:
                    return true;
                default:
                    return false;
            }
        }

        public static byte[] GetUncompressedRawBytes(Bitmap bmp, TextureFormat format) {
            int w = bmp.Width;
            int h = bmp.Height;
            var bmpData = bmp.LockBits(new Rectangle(0, 0, w, h), System.Drawing.Imaging.ImageLockMode.ReadOnly, System.Drawing.Imaging.PixelFormat.Format32bppArgb);
            byte[] rawBgra = new byte[w * h * 4];
            System.Runtime.InteropServices.Marshal.Copy(bmpData.Scan0, rawBgra, 0, rawBgra.Length);
            bmp.UnlockBits(bmpData);

            if (format == TextureFormat.D3DFMT_A8R8G8B8) return rawBgra;
            if (format == TextureFormat.D3DFMT_A8) {
                byte[] a8 = new byte[w * h];
                for (int i = 0; i < w * h; i++) a8[i] = rawBgra[i * 4 + 3]; // Alpha channel
                return a8;
            }
            if (format == TextureFormat.D3DFMT_A1R5G5B5) {
                byte[] a1r5g5b5 = new byte[w * h * 2];
                for (int i = 0; i < w * h; i++) {
                    int b = rawBgra[i * 4] >> 3;
                    int g = rawBgra[i * 4 + 1] >> 3;
                    int r = rawBgra[i * 4 + 2] >> 3;
                    int a = rawBgra[i * 4 + 3] >> 7;
                    ushort val = (ushort)((a << 15) | (r << 10) | (g << 5) | b);
                    a1r5g5b5[i * 2] = (byte)(val & 0xFF);
                    a1r5g5b5[i * 2 + 1] = (byte)(val >> 8);
                }
                return a1r5g5b5;
            }
            return rawBgra;
        }
    }
}
