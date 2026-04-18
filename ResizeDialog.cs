using System;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Windows.Forms;

namespace EasyOptimizerV
{
    public class ResizeDialog : Form
    {
        public int NewWidth { get; private set; }
        public int NewHeight { get; private set; }
        public string SelectedFormat { get; private set; }
        public int NewMips { get; private set; }

        private NumericUpDown widthInput;
        private NumericUpDown heightInput;
        private CheckBox lockAspect;
        private ComboBox formatCombo;
        private NumericUpDown mipsInput;
        private Button okButton;
        private Button cancelButton;

        private bool isUpdating = false;
        private double aspectRatio;

        public ResizeDialog(int currentWidth, int currentHeight)
        {
            this.Text = "Resize Texture";
            this.Size = new Size(420, 520); // Bit more height for safety
            this.FormBorderStyle = FormBorderStyle.FixedDialog;
            this.MaximizeBox = false;
            this.MinimizeBox = false;
            this.StartPosition = FormStartPosition.CenterParent;
            this.BackColor = Color.FromArgb(28, 28, 28);
            
            Theme.Apply(this);

            int padX = 35;
            int inputX = 145;
            int inputW = 230; 
            int startY = 100;
            int spacing = 65;

            // 1. Header
            Label lblHeader = new Label() {
                Text = "Resize Settings",
                Font = new Font("Segoe UI", 12F, FontStyle.Bold),
                ForeColor = Color.White,
                Location = new Point(padX, 25),
                AutoSize = true
            };
            this.Controls.Add(lblHeader);

            Panel headerLine = new Panel() {
                BackColor = Color.FromArgb(60, 60, 60),
                Size = new Size(this.Width - 70, 1),
                Location = new Point(padX, 65)
            };
            this.Controls.Add(headerLine);

            Label CreateLabel(string text, int y) {
                 return new Label() { 
                    Text = text, 
                    Location = new Point(padX, y + 10), 
                    ForeColor = Color.FromArgb(160, 160, 160),
                    Font = new Font("Segoe UI", 10F),
                    AutoSize = true
                };
            }

            // EXTREME CLIPPING INPUT SYSTEM
            Control CreateInput(Control input, int y, int w, bool showIndicator = true) {
                Panel container = new Panel();
                container.Location = new Point(inputX, y);
                container.Size = new Size(w, 42); 
                container.BackColor = Color.Transparent;

                int indicatorSpace = showIndicator ? 35 : 0;

                // Mask - Small window that cuts off all native WinForms borders
                Panel maskPanel = new Panel();
                maskPanel.Location = new Point(8, 8); // more internal padding to fix "numbers inside"
                maskPanel.Size = new Size(w - indicatorSpace - 16, 24); // Tighter height to CLIP borders
                maskPanel.BackColor = Color.FromArgb(40, 40, 40);
                
                input.BackColor = Color.FromArgb(40, 40, 40);
                input.ForeColor = Color.White;
                input.Font = new Font("Segoe UI", 10F);
                
                if (input is NumericUpDown nud) {
                    nud.BorderStyle = BorderStyle.None;
                    nud.Location = new Point(8, 2); 
                    nud.Width = w + 100; 
                }
                else if (input is ComboBox cb) {
                    cb.FlatStyle = FlatStyle.Flat;
                    // Centered Y position while keeping bottom border BELOW the 24px mask
                    cb.Location = new Point(-2, -3);
                    cb.Width = maskPanel.Width + 40; 
                    cb.Height = 35;
                    cb.DropDownHeight = 300;
                }
                
                maskPanel.Controls.Add(input);
                container.Controls.Add(maskPanel);

                container.Paint += (s, e) => {
                    e.Graphics.SmoothingMode = SmoothingMode.AntiAlias;
                    Rectangle rect = new Rectangle(0, 0, container.Width - 1, container.Height - 1);
                    
                    using (GraphicsPath path = Theme.CreateRoundedRect(rect, 10)) {
                        using (SolidBrush brush = new SolidBrush(Color.FromArgb(40, 40, 40))) e.Graphics.FillPath(brush, path);
                        using (Pen pen = new Pen(Color.FromArgb(65, 65, 65), 1)) e.Graphics.DrawPath(pen, path);
                    }

                    if (showIndicator) {
                        using (Pen sepPen = new Pen(Color.FromArgb(50, 50, 50), 1))
                            e.Graphics.DrawLine(sepPen, container.Width - 35, 12, container.Width - 35, container.Height - 12);

                        using (Pen arrowPen = new Pen(Color.FromArgb(160, 160, 160), 2)) {
                            arrowPen.StartCap = LineCap.Round;
                            arrowPen.EndCap = LineCap.Round;
                            int ax = container.Width - 17;
                            
                            if (input is ComboBox) {
                                int ay = container.Height / 2 - 1;
                                e.Graphics.DrawLines(arrowPen, new Point[] { new Point(ax-4, ay), new Point(ax, ay+4), new Point(ax+4, ay) });
                            } else {
                                int uy = container.Height / 2 - 7;
                                e.Graphics.DrawLines(arrowPen, new Point[] { new Point(ax-3, uy+3), new Point(ax, uy), new Point(ax+3, uy+3) });
                                int dy = container.Height / 2 + 7;
                                e.Graphics.DrawLines(arrowPen, new Point[] { new Point(ax-3, dy-3), new Point(ax, dy), new Point(ax+3, dy-3) });
                            }
                        }
                    }
                };

                // Interaction
                container.MouseClick += (s, e) => {
                    if (showIndicator && e.X > container.Width - 35) {
                        if (input is NumericUpDown nud) {
                            if (e.Y < container.Height/2) { if (nud.Value < nud.Maximum) nud.Value++; }
                            else { if (nud.Value > nud.Minimum) nud.Value--; }
                        } else if (input is ComboBox cb) {
                            cb.DroppedDown = !cb.DroppedDown;
                        }
                    } else {
                        input.Focus();
                        if (input is ComboBox cb) cb.DroppedDown = !cb.DroppedDown;
                    }
                };

                return container;
            }

            // ROWS
            this.Controls.Add(CreateLabel("Width", startY));
            widthInput = new NumericUpDown() { Minimum = 1, Maximum = 8192, Value = currentWidth };
            this.Controls.Add(CreateInput(widthInput, startY, inputW, false));

            this.Controls.Add(CreateLabel("Height", startY + spacing));
            heightInput = new NumericUpDown() { Minimum = 1, Maximum = 8192, Value = currentHeight };
            this.Controls.Add(CreateInput(heightInput, startY + spacing, inputW, false));

            aspectRatio = (double)currentWidth / currentHeight;

            lockAspect = new CheckBox() {
                Text = "Lock Aspect Ratio",
                Checked = true,
                ForeColor = Color.FromArgb(160, 160, 160),
                Location = new Point(inputX, startY + spacing + 48),
                AutoSize = true,
                Font = new Font("Segoe UI", 8.5F)
            };
            this.Controls.Add(lockAspect);

            widthInput.ValueChanged += (s, e) => {
                if (lockAspect.Checked && !isUpdating) {
                    isUpdating = true;
                    heightInput.Value = Math.Max(1, Math.Min(8192, (int)Math.Round(widthInput.Value / (decimal)aspectRatio)));
                    isUpdating = false;
                }
            };

            heightInput.ValueChanged += (s, e) => {
                if (lockAspect.Checked && !isUpdating) {
                    isUpdating = true;
                    widthInput.Value = Math.Max(1, Math.Min(8192, (int)Math.Round(heightInput.Value * (decimal)aspectRatio)));
                    isUpdating = false;
                }
            };

            this.Controls.Add(CreateLabel("Format", startY + spacing * 2));
            formatCombo = new ComboBox();
            formatCombo.DropDownStyle = ComboBoxStyle.DropDownList;
            formatCombo.FlatStyle = FlatStyle.Flat;
            formatCombo.DrawMode = DrawMode.OwnerDrawFixed; 
            formatCombo.ItemHeight = 24;
            formatCombo.Items.AddRange(new object[] { 
                "Preserve Original", "DXT1", "DXT3", "DXT5", "ATI1", "ATI2", "BC7", "A8R8G8B8", "A1R5G5B5", "A8" 
            });
            formatCombo.SelectedIndex = 0;
            formatCombo.DrawItem += (s, e) => {
                if (e.Index < 0) return;
                Color bgColor = (e.State.HasFlag(DrawItemState.Selected)) ? Theme.Primary : Color.FromArgb(40, 40, 40);
                e.Graphics.FillRectangle(new SolidBrush(bgColor), e.Bounds);
                e.Graphics.DrawString(formatCombo.Items[e.Index].ToString(), formatCombo.Font, Brushes.White, e.Bounds.X + 8, e.Bounds.Y + 4);
                // NEVER call e.DrawFocusRectangle() here as it draws white dotted borders
            };
            this.Controls.Add(CreateInput(formatCombo, startY + spacing * 2, inputW, true));

            this.Controls.Add(CreateLabel("Mipmaps", startY + spacing * 3));
            mipsInput = new NumericUpDown() { Minimum = -2, Maximum = 16, Value = -1 };
            this.Controls.Add(CreateInput(mipsInput, startY + spacing * 3, 110, true)); 
            
            Label lblHint = new Label() { 
                Text = "(-1 = Auto / -2 = Original)", 
                Location = new Point(inputX + 125, startY + spacing * 3 + 12),
                ForeColor = Color.FromArgb(130, 130, 130), 
                Font = new Font("Segoe UI", 8.5F),
                AutoSize = true
            };
            this.Controls.Add(lblHint);

            // FOOTER
            int btnY = this.ClientSize.Height - 80;
            okButton = new Button() { Text = "Apply Changes", Size = new Size(150, 44), DialogResult = DialogResult.OK };
            okButton.Location = new Point(this.ClientSize.Width - 150 - 35, btnY);
            okButton.FlatStyle = FlatStyle.Flat;
            okButton.BackColor = Theme.Primary;
            okButton.ForeColor = Color.White;
            okButton.Font = new Font("Segoe UI", 10F, FontStyle.Bold);
            okButton.Cursor = Cursors.Hand;
            okButton.FlatAppearance.BorderSize = 0;
            okButton.FlatAppearance.BorderColor = Color.FromArgb(0, 0, 0, 0); // Fully Transparent Border
            okButton.FlatAppearance.CheckedBackColor = Theme.Primary;
            okButton.NotifyDefault(false);
            okButton.Anchor = AnchorStyles.Bottom | AnchorStyles.Right;
            okButton.Click += (s, e) => { 
                NewWidth = (int)widthInput.Value; 
                NewHeight = (int)heightInput.Value; 
                SelectedFormat = formatCombo.SelectedItem.ToString();
                NewMips = (int)mipsInput.Value;
            };

            cancelButton = new Button() { Text = "Cancel", Size = new Size(100, 44), DialogResult = DialogResult.Cancel };
            cancelButton.Location = new Point(okButton.Left - 110, btnY);
            cancelButton.FlatStyle = FlatStyle.Flat;
            cancelButton.BackColor = Color.Transparent;
            cancelButton.ForeColor = Color.FromArgb(180, 180, 180);
            cancelButton.Font = new Font("Segoe UI", 10F);
            cancelButton.Cursor = Cursors.Hand;
            cancelButton.FlatAppearance.BorderSize = 1;
            cancelButton.FlatAppearance.BorderColor = Color.FromArgb(70, 70, 70);
            cancelButton.NotifyDefault(false);
            cancelButton.Anchor = AnchorStyles.Bottom | AnchorStyles.Right;

            this.Controls.Add(okButton);
            this.Controls.Add(cancelButton);
            
            this.AcceptButton = okButton;
            this.CancelButton = cancelButton;
            
            this.Activated += (s, e) => { lblHeader.Focus(); };
        }
    }
}
