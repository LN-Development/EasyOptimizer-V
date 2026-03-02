using System;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Windows.Forms;
using System.ComponentModel;
using CodeWalker.GameFiles;

namespace EasyOptimizerV
{
    public class TextureCard : UserControl
    {
        private Texture texture;
        private YtdFile parentYtd;
        private Image originalImage;
        private Image thumbImage;
        
        public string TextureName => texture?.Name ?? "";
        public Texture TextureObject => texture;
        public string ParentYtdName => parentYtd?.Name ?? "";

        private bool isHovered = false;
        private Rectangle editButtonRect;
        [Browsable(false)]
        [DesignerSerializationVisibility(DesignerSerializationVisibility.Hidden)]
        public string DuplicateType { get; set; } = ""; // "", "Name", or "Hex"

        [Browsable(false)]
        [DesignerSerializationVisibility(DesignerSerializationVisibility.Hidden)]
        public System.Collections.Generic.List<string> OtherYtds { get; set; } = new System.Collections.Generic.List<string>();

        public event Action<Texture> OnResizeRequested;

        public TextureCard(Texture tex, Image img, YtdFile parent)
        {
            this.texture = tex;
            this.parentYtd = parent;
            this.originalImage = img;
            this.DoubleBuffered = true;
            this.Size = new Size(220, 260); // Aspect Square-ish with bottom
            this.BackColor = Color.Transparent; // Draw rounded shape
            // this.Cursor = Cursors.Hand; // Cursor will be handled by MouseMove
            this.Margin = new Padding(8); // Grid Gap

            // Context Menu
            var ctx = new ContextMenuStrip();
            ctx.Renderer = new ToolStripProfessionalRenderer(new DarkColorTable()) { RoundedEdges = false };
            ctx.BackColor = Theme.SurfaceDark;
            ctx.ForeColor = Theme.TextPrimaryDark;
            var itemResize = new ToolStripMenuItem("Resize...", null, (s, e) => OnResizeRequested?.Invoke(texture));
            itemResize.ForeColor = Theme.TextPrimaryDark;
            ctx.Items.Add(itemResize);
            this.ContextMenuStrip = ctx;

            this.MouseEnter += (s, e) => { isHovered = true; Invalidate(); };
            this.MouseLeave += (s, e) => { isHovered = false; Invalidate(); };
            this.MouseMove += TextureCard_MouseMove;
            this.MouseClick += TextureCard_MouseClick;
        }

        private void TextureCard_MouseMove(object? sender, MouseEventArgs e)
        {
            if (editButtonRect.Contains(e.Location))
            {
                this.Cursor = Cursors.Hand;
            }
            else
            {
                this.Cursor = Cursors.Default;
            }
            Invalidate(editButtonRect); // Invalidate only the button area for hover effect
        }

        private void TextureCard_MouseClick(object? sender, MouseEventArgs e)
        {
            if (e.Button == MouseButtons.Left && editButtonRect.Contains(e.Location))
            {
                OnResizeRequested?.Invoke(texture);
            }
        }

        protected override void OnPaint(PaintEventArgs e)
        {
            e.Graphics.SmoothingMode = SmoothingMode.AntiAlias;
            e.Graphics.TextRenderingHint = System.Drawing.Text.TextRenderingHint.ClearTypeGridFit;

            // 1. Draw Card Background (Rounded)
            Rectangle rect = new Rectangle(0, 0, this.Width - 1, this.Height - 1);
            using (GraphicsPath path = Theme.CreateRoundedRect(rect, 8)) // 8px Radius (Template: rounded-xl)
            {
                using (SolidBrush bgBrush = new SolidBrush(Theme.SurfaceDark))
                {
                    e.Graphics.FillPath(bgBrush, path);
                }
                
                // Border
                using (Pen borderPen = new Pen(Theme.BorderDark, 1))
                {
                    if (isHovered) borderPen.Color = Theme.Primary; // Hover effect
                    e.Graphics.DrawPath(borderPen, path);
                }

                // 2. Draw Image (Top - 80px for text)
                e.Graphics.SetClip(path);
                
                int footerHeight = 80;
                int imgHeight = this.Height - footerHeight;
                Rectangle imgRect = new Rectangle(0, 0, this.Width, imgHeight);
                
                // Draw background for transparent images
                using (SolidBrush imgBg = new SolidBrush(Color.Black)) // Template uses black/gray bg
                {
                    e.Graphics.FillRectangle(imgBg, imgRect);
                }

                if (originalImage != null)
                {
                    // Center Zoom
                    float ratio = Math.Min((float)imgRect.Width / originalImage.Width, (float)imgRect.Height / originalImage.Height);
                    int w = (int)(originalImage.Width * ratio);
                    int h = (int)(originalImage.Height * ratio);
                    int x = (imgRect.Width - w) / 2;
                    int y = (imgRect.Height - h) / 2;
                    e.Graphics.DrawImage(originalImage, x, y, w, h);
                }

                // 3. Format Badge (Top Right)
                string fmt = texture.Format.ToString().Replace("D3DFMT_", "");
                // Badge Style: bg-black/60 backdrop-blur-sm px-2 py-0.5 rounded text-[10px] text-white font-mono
                Font badgeFont = Theme.FontMono;
                SizeF badgeSize = e.Graphics.MeasureString(fmt, badgeFont);
                int padX = 4;
                int padY = 2;
                Rectangle badgeRect = new Rectangle(
                    imgRect.Right - (int)badgeSize.Width - padX * 2 - 8,
                    imgRect.Top + 8,
                    (int)badgeSize.Width + padX * 2,
                    (int)badgeSize.Height + padY * 2
                );

                using (GraphicsPath badgePath = Theme.CreateRoundedRect(badgeRect, 3))
                {
                    using (SolidBrush badgeBg = new SolidBrush(Color.FromArgb(150, 0, 0, 0))) // Black/60
                    {
                        e.Graphics.FillPath(badgeBg, badgePath);
                    }
                    e.Graphics.DrawString(fmt, badgeFont, Brushes.White, badgeRect.X + padX, badgeRect.Y + padY);
                }

                // 3.5 Duplicate Badge (Top Left)
                if (!string.IsNullOrEmpty(DuplicateType))
                {
                    string dupText = DuplicateType == "Hex" ? "HEX DUP" : "NAME DUP";
                    Color dupColor = DuplicateType == "Hex" ? Color.FromArgb(220, 38, 38) : Color.FromArgb(234, 179, 8); // Red vs Yellow
                    
                    Font dupFont = new Font("Segoe UI", 7F, FontStyle.Bold);
                    SizeF dupSize = e.Graphics.MeasureString(dupText, dupFont);
                    Rectangle dupRect = new Rectangle(imgRect.Left + 8, imgRect.Top + 8, (int)dupSize.Width + 8, (int)dupSize.Height + 4);
                    
                    using (GraphicsPath dp = Theme.CreateRoundedRect(dupRect, 3))
                    {
                        using (SolidBrush db = new SolidBrush(dupColor)) e.Graphics.FillPath(db, dp);
                        e.Graphics.DrawString(dupText, dupFont, Brushes.White, dupRect.X + 4, dupRect.Y + 2);
                    }
                }

                e.Graphics.ResetClip();

                // 4. Text Content (Bottom Panel)
                int textY = imgHeight + 12;
                int textX = 12;

                // Name (Truncated) H3
                // Name (Truncated) H3
                string name = texture.Name;
                
                // Calculate Memory Size
                long sizeBytes = 0;
                if (texture.Data != null) sizeBytes = texture.Data.FullData.Length;
                else sizeBytes = (long)(texture.Width * texture.Height * 4 * 1.33); // Fallback calc

                double mib = sizeBytes / (1024.0 * 1024.0);
                string sizeText = $"{mib:F2} MiB";

                // Color Logic
                Color sizeColor = Color.FromArgb(22, 163, 74); // Default Green (<= 0.7)
                
                if (mib <= 0.70) sizeColor = Color.FromArgb(22, 163, 74);      // Green
                else if (mib <= 1.0) sizeColor = Color.FromArgb(74, 222, 128); // Light Green
                else if (mib <= 1.5) sizeColor = Color.FromArgb(253, 224, 71); // Light Yellow
                else if (mib <= 2.0) sizeColor = Color.FromArgb(234, 179, 8);  // Yellow
                else if (mib <= 2.5) sizeColor = Color.FromArgb(251, 146, 60); // Light Orange
                else if (mib <= 3.5) sizeColor = Color.FromArgb(249, 115, 22); // Orange
                else if (mib < 4.5) sizeColor = Color.FromArgb(239, 68, 68);   // Light Red (>= 4.0 covers this range)
                else sizeColor = Color.FromArgb(220, 38, 38);                  // Red (>= 4.5)

                // Draw Name
                using (SolidBrush textBrush = new SolidBrush(Theme.TextPrimaryDark))
                {
                    StringFormat sf = new StringFormat(StringFormatFlags.NoWrap);
                    sf.Trimming = StringTrimming.EllipsisCharacter;
                    // Leaves space for size text (approx 60px)
                    Rectangle nameRect = new Rectangle(textX, textY, this.Width - textX * 2 - 70, 20);
                    e.Graphics.DrawString(name, Theme.FontTitle, textBrush, nameRect, sf);
                }

                // Draw Size
                using (SolidBrush sizeBrush = new SolidBrush(sizeColor))
                {
                    // Right aligned in the row
                    StringFormat sfSize = new StringFormat();
                    sfSize.Alignment = StringAlignment.Far;
                    Rectangle sizeRect = new Rectangle(this.Width - 80, textY, 70, 20);
                    e.Graphics.DrawString(sizeText, Theme.FontSmallBold, sizeBrush, sizeRect, sfSize);
                }

                // Dimensions & Mips (Secondary)
                textY += 24;
                string meta = $"{texture.Width} x {texture.Height}";
                string mips = $"Mips: {texture.Levels}";
                
                // Layout: Dimensions Left, Mips below/right? Template: stacked.
                /*
                  Dimensions
                  Mips: X
                */
                using (SolidBrush textSec = new SolidBrush(Theme.TextSecondaryDark))
                {
                    e.Graphics.DrawString(meta, Theme.FontSmall, textSec, textX, textY);
                    e.Graphics.DrawString(mips, Theme.FontSmall, textSec, textX, textY + 14);
                    // Show Parent YTD Name
                    string parentName = parentYtd.Name ?? "Unknown";
                    e.Graphics.DrawString(parentName, Theme.FontSmall, textSec, textX, textY + 28);
                }

                // Edit Button (Bottom Right)
                int btnW = 60;
                int btnH = 24;
                editButtonRect = new Rectangle(this.Width - btnW - 12, this.Height - btnH - 12, btnW, btnH);
                
                // Button Style
                bool isMouseOverEdit = editButtonRect.Contains(this.PointToClient(Cursor.Position));
                
                using (GraphicsPath btnPath = Theme.CreateRoundedRect(editButtonRect, 4))
                using (SolidBrush btnBrush = new SolidBrush(isMouseOverEdit ? Theme.Primary : Color.FromArgb(60, 60, 60)))
                {
                     e.Graphics.FillPath(btnBrush, btnPath);
                }
                
                using (SolidBrush textBrush = new SolidBrush(Color.White))
                {
                    StringFormat sfBtn = new StringFormat();
                    sfBtn.Alignment = StringAlignment.Center;
                    sfBtn.LineAlignment = StringAlignment.Center;
                    e.Graphics.DrawString("Edit", Theme.FontSmall, textBrush, editButtonRect, sfBtn);
                }
            }
        }
    }
}
