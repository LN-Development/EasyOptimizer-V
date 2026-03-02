using System;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Windows.Forms;
using CodeWalker.GameFiles;

namespace EasyOptimizerV
{
    public class YtdFolderCard : UserControl
    {
        public YtdFile? Ytd { get; }
        public string? VirtualId { get; }
        public string? VirtualName { get; }
        public string FileType { get; } = "YTD";
        [System.ComponentModel.DesignerSerializationVisibility(System.ComponentModel.DesignerSerializationVisibility.Hidden)]
        public bool IsExpanded { get; set; }
        public event Action<YtdFolderCard>? OnToggleRequested;

        private bool isHovered = false;
        private string totalSizeInfo = "";
        private Color statusColor = Color.Gray;
        private double totalMib = 0;

        public YtdFolderCard(YtdFile ytd, bool isExpanded, int width, string fileType = "YTD")
        {
            this.Ytd = ytd;
            this.FileType = fileType;
            this.IsExpanded = isExpanded;
            this.Width = width;
            this.Height = 60; // Thinner header-like height
            this.Margin = new Padding(0, 0, 0, 4);
            this.DoubleBuffered = true;
            this.Cursor = Cursors.Hand;

            // Calculate Total Size
            long bytes = 0;
            if (ytd.TextureDict?.Textures?.data_items != null)
            {
                foreach (var tex in ytd.TextureDict.Textures.data_items)
                {
                    if (tex.Data?.FullData != null) bytes += tex.Data.FullData.Length;
                }
            }
            double mib = bytes / 1024.0 / 1024.0;
            totalMib = mib;
            totalSizeInfo = mib > 0.01 ? $"{mib:F2} MiB" : $"{bytes / 1024.0:F1} KiB";

            // Color Coding Logic
            if (mib <= 16) statusColor = ColorTranslator.FromHtml("#0047AB"); // Cobalt Blue
            else if (mib <= 32) statusColor = ColorTranslator.FromHtml("#FFFACD"); // Light Yellow
            else if (mib <= 48) statusColor = Color.Orange; // Gap bridging
            else statusColor = Color.Red; // Strong Red (> 48)

            this.MouseEnter += (s, e) => { isHovered = true; Invalidate(); };
            this.MouseLeave += (s, e) => { isHovered = false; Invalidate(); };
            this.Click += (s, e) => OnToggleRequested?.Invoke(this);
        }

        public YtdFolderCard(string virtualId, string name, string info, Color color, bool isExpanded, int width)
        {
            this.VirtualId = virtualId;
            this.VirtualName = name;
            this.totalSizeInfo = info;
            this.statusColor = color;
            this.IsExpanded = isExpanded;
            this.Width = width;
            this.Height = 60;
            this.Margin = new Padding(0, 0, 0, 4);
            this.DoubleBuffered = true;
            this.Cursor = Cursors.Hand;

            this.MouseEnter += (s, e) => { isHovered = true; Invalidate(); };
            this.MouseLeave += (s, e) => { isHovered = false; Invalidate(); };
            this.Click += (s, e) => OnToggleRequested?.Invoke(this);
        }

        protected override void OnPaint(PaintEventArgs e)
        {
            var g = e.Graphics;
            g.SmoothingMode = SmoothingMode.AntiAlias;

            var rect = new Rectangle(0, 0, Width - 1, Height - 1);
            int radius = 8;

            // Background
            using (var path = Theme.CreateRoundedRect(rect, radius))
            {
                using (var brush = new SolidBrush(isHovered ? Color.FromArgb(40, 40, 40) : Theme.SurfaceDark))
                {
                    g.FillPath(brush, path);
                }

                if (IsExpanded)
                {
                    using (var pen = new Pen(Theme.Primary, 1))
                    {
                        g.DrawPath(pen, path);
                    }
                }
            }

            // Folder Icon (Simple drawing)
            int iconSize = 32;
            int margin = 16;
            Rectangle iconRect = new Rectangle(margin, (Height - iconSize) / 2, iconSize, iconSize);
            
            using (var iconBrush = new SolidBrush(statusColor))
            {
                // Simple folder shape
                g.FillRectangle(iconBrush, iconRect.X, iconRect.Y + 6, iconSize, iconSize - 6);
                g.FillRectangle(iconBrush, iconRect.X, iconRect.Y, iconSize / 2, 8);
            }

            // File type badge (YTD = blue, WTD = amber)
            float badgeX = iconRect.Right + 12;
            if (Ytd != null && FileType != null)
            {
                Color badgeColor = FileType == "WTD" ? Color.FromArgb(255, 179, 71) : Color.FromArgb(60, 120, 216);
                string badgeText = FileType;
                SizeF badgeSize = g.MeasureString(badgeText, Theme.FontSmallBold);
                RectangleF badgeRect = new RectangleF(badgeX, 13, badgeSize.Width + 8, badgeSize.Height + 2);
                using (var badgeBrush = new SolidBrush(Color.FromArgb(40, badgeColor)))
                    g.FillRectangle(badgeBrush, badgeRect);
                using (var badgePen = new Pen(badgeColor, 1))
                    g.DrawRectangle(badgePen, badgeRect.X, badgeRect.Y, badgeRect.Width, badgeRect.Height);
                using (var badgeTextBrush = new SolidBrush(badgeColor))
                    g.DrawString(badgeText, Theme.FontSmallBold, badgeTextBrush, badgeRect.X + 4, badgeRect.Y + 1);
                badgeX += badgeRect.Width + 6;
            }

            // Text info
            string name = VirtualName ?? Ytd?.Name ?? "Unknown.ytd";
            string infoText = totalSizeInfo;
            if (Ytd != null)
            {
                int count = Ytd.TextureDict?.Textures?.data_items?.Length ?? 0;
                infoText = $"{count} items  •  {totalSizeInfo}";
            }

            float textX = badgeX;
            g.DrawString(name, Theme.FontTitle, Brushes.White, textX, 12);

            // Item count/info in default color (with MiB highlighting if applicable)
            if (Ytd != null)
            {
                int count = Ytd.TextureDict?.Textures?.data_items?.Length ?? 0;
                string prefix = $"{count} items  •  ";
                SizeF prefixSize = g.MeasureString(prefix, Theme.FontDisplay);
                g.DrawString(prefix, Theme.FontDisplay, IsExpanded ? Brushes.White : Brushes.Gray, textX, 32);
                g.DrawString(totalSizeInfo, Theme.FontDisplay, new SolidBrush(statusColor), textX + prefixSize.Width - 5, 32);
            }
            else
            {
                g.DrawString(infoText, Theme.FontDisplay, new SolidBrush(statusColor), textX, 32);
            }

            // Expand/Collapse arrow
            string arrow = IsExpanded ? "▼" : "▶";
            g.DrawString(arrow, Theme.FontTitle, Brushes.White, Width - 30, (Height - 20) / 2);
        }
    }
}
