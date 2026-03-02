using System;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Windows.Forms;

namespace EasyOptimizerV
{
    public static class Theme
    {
        // Template Colors (#RRGGBB)
        public static Color BackgroundDark = ColorTranslator.FromHtml("#181818");
        public static Color SurfaceDark = ColorTranslator.FromHtml("#202020");
        public static Color BorderDark = ColorTranslator.FromHtml("#333333");
        public static Color Primary = ColorTranslator.FromHtml("#0078D4"); // Fluent Blue
        public static Color TextPrimaryDark = ColorTranslator.FromHtml("#FFFFFF");
        public static Color TextSecondaryDark = ColorTranslator.FromHtml("#A1A1A1");
        
        // Font
        public static Font FontDisplay = new Font("Segoe UI", 10F, FontStyle.Regular);
        public static Font FontTitle = new Font("Segoe UI", 11F, FontStyle.Bold); // H3
        public static Font FontMono = new Font("Consolas", 8F); // Format Badge
        public static Font FontSmall = new Font("Segoe UI", 8F); // Dimensions
        public static Font FontSmallBold = new Font("Segoe UI", 8F, FontStyle.Bold);

        public static void Apply(Form form)
        {
            form.BackColor = BackgroundDark;
            form.ForeColor = TextPrimaryDark;
            form.Font = FontDisplay;
        }

        public static GraphicsPath CreateRoundedRect(Rectangle rect, int radius)
        {
            GraphicsPath path = new GraphicsPath();
            int d = radius * 2;
            path.AddArc(rect.X, rect.Y, d, d, 180, 90);
            path.AddArc(rect.X + rect.Width - d, rect.Y, d, d, 270, 90);
            path.AddArc(rect.X + rect.Width - d, rect.Y + rect.Height - d, d, d, 0, 90);
            path.AddArc(rect.X, rect.Y + rect.Height - d, d, d, 90, 90);
            path.CloseFigure();
            return path;
        }
    }

    public class DarkColorTable : ProfessionalColorTable
    {
        public override Color MenuStripGradientBegin => Theme.SurfaceDark;
        public override Color MenuStripGradientEnd => Theme.SurfaceDark;
        public override Color ToolStripGradientBegin => Theme.SurfaceDark;
        public override Color ToolStripGradientMiddle => Theme.SurfaceDark;
        public override Color ToolStripGradientEnd => Theme.SurfaceDark;
        public override Color ToolStripBorder => Theme.BorderDark;
        public override Color MenuBorder => Theme.BorderDark;
        public override Color MenuItemSelected => Color.FromArgb(60, 60, 60);
        public override Color MenuItemBorder => Color.FromArgb(60, 60, 60);
        public override Color MenuItemSelectedGradientBegin => Color.FromArgb(60, 60, 60);
        public override Color MenuItemSelectedGradientEnd => Color.FromArgb(60, 60, 60);
        public override Color MenuItemPressedGradientBegin => Theme.BackgroundDark;
        public override Color MenuItemPressedGradientEnd => Theme.BackgroundDark;
        public override Color ImageMarginGradientBegin => Theme.SurfaceDark;
        public override Color ImageMarginGradientMiddle => Theme.SurfaceDark;
        public override Color ImageMarginGradientEnd => Theme.SurfaceDark;
        public override Color SeparatorDark => Theme.BorderDark;
        public override Color SeparatorLight => Theme.BorderDark;
    }
}
