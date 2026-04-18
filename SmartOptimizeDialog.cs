using System;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Windows.Forms;

namespace EasyOptimizerV
{
    public class SmartOptimizeDialog : Form
    {
        public bool OptimizeBySize { get; private set; }
        public float TargetMiB { get; private set; }
        public int MaxResolution { get; private set; }
        public string PreferredFormat { get; private set; }

        private NumericUpDown mibInput;
        private ComboBox resCombo;
        private ComboBox formatCombo;
        private RadioButton sizeRadio;
        private RadioButton resRadio;
        private Button okButton;
        private Button cancelButton;

        public SmartOptimizeDialog()
        {
            this.Text = "Smart Optimize";
            this.Size = new Size(420, 580);
            this.FormBorderStyle = FormBorderStyle.FixedDialog;
            this.MaximizeBox = false;
            this.MinimizeBox = false;
            this.StartPosition = FormStartPosition.CenterParent;
            this.BackColor = Theme.BackgroundDark;
            
            Theme.Apply(this);

            int padX = 35;
            int inputX = 145;
            int inputW = 230; 
            int startY = 100;
            int spacing = 80;

            // Header
            Label lblHeader = new Label() {
                Text = "Smart Optimization Settings",
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
                    Location = new Point(padX + 20, y + 10), 
                    ForeColor = Color.FromArgb(160, 160, 160),
                    Font = new Font("Segoe UI", 10F),
                    AutoSize = true
                };
            }

            // Radio Buttons
            sizeRadio = new RadioButton() {
                Text = "Target MiB (File Size)",
                Location = new Point(padX, startY - 30),
                ForeColor = Color.White,
                Font = new Font("Segoe UI", 10F, FontStyle.Bold),
                Checked = true,
                AutoSize = true
            };
            this.Controls.Add(sizeRadio);

            resRadio = new RadioButton() {
                Text = "Cap Maximum Resolution",
                Location = new Point(padX, startY + spacing + 20),
                ForeColor = Color.White,
                Font = new Font("Segoe UI", 10F, FontStyle.Bold),
                AutoSize = true
            };
            this.Controls.Add(resRadio);

            // Inputs Holder (Internal Masking copied from ResizeDialog style)
            Control CreateInput(Control input, int y, int w, bool showIndicator = true) {
                Panel container = new Panel();
                container.Location = new Point(inputX, y);
                container.Size = new Size(w, 42); 
                container.BackColor = Color.Transparent;

                Panel maskPanel = new Panel();
                maskPanel.Location = new Point(8, 8);
                maskPanel.Size = new Size(w - (showIndicator ? 35 : 0) - 16, 24);
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
                    cb.Location = new Point(-2, -3);
                    cb.Width = maskPanel.Width + 40; 
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
                };
                return container;
            }

            // Size Row
            this.Controls.Add(CreateLabel("Max MiB", startY));
            mibInput = new NumericUpDown() { Minimum = 1, Maximum = 1000, Value = 16, DecimalPlaces = 1 };
            this.Controls.Add(CreateInput(mibInput, startY, 120, false));

            // Res Row
            this.Controls.Add(CreateLabel("Max Dim", startY + spacing + 50));
            resCombo = new ComboBox();
            resCombo.DropDownStyle = ComboBoxStyle.DropDownList;
            resCombo.Items.AddRange(new object[] { "4096", "2048", "1024", "512", "256", "128" });
            resCombo.SelectedIndex = 2; // 1024
            this.Controls.Add(CreateInput(resCombo, startY + spacing + 50, 120, true));

            // Format Row
            this.Controls.Add(new Label() { 
                Text = "Preferred Format (Optional)", 
                Location = new Point(padX, startY + spacing * 2 + 80),
                ForeColor = Color.FromArgb(160, 160, 160),
                AutoSize = true 
            });
            formatCombo = new ComboBox();
            formatCombo.DropDownStyle = ComboBoxStyle.DropDownList;
            formatCombo.Items.AddRange(new object[] { "Original", "DXT1 (No Alpha)", "DXT5 (Alpha)", "ATI2 (Normal)", "BC7 (High Quality)" });
            formatCombo.SelectedIndex = 0;
            this.Controls.Add(CreateInput(formatCombo, startY + spacing * 2 + 110, inputW, true));

            // Helper text
            Label lblSizeTip = new Label() { 
                Text = "Will downscale largest textures until target MiB is reached.",
                Location = new Point(padX + 20, startY + 45),
                ForeColor = Color.FromArgb(100, 100, 100),
                Font = new Font("Segoe UI", 8F),
                Size = new Size(inputW, 40)
            };
            this.Controls.Add(lblSizeTip);

            Label lblResTip = new Label() { 
                Text = "Caps all textures at this resolution.",
                Location = new Point(padX + 20, startY + spacing + 95),
                ForeColor = Color.FromArgb(100, 100, 100),
                Font = new Font("Segoe UI", 8F),
                Size = new Size(inputW, 40)
            };
            this.Controls.Add(lblResTip);

            // Disable logic
            sizeRadio.CheckedChanged += (s, e) => {
                mibInput.Enabled = sizeRadio.Checked;
                resCombo.Enabled = resRadio.Checked;
            };
            resRadio.CheckedChanged += (s, e) => {
                mibInput.Enabled = sizeRadio.Checked;
                resCombo.Enabled = resRadio.Checked;
            };
            resCombo.Enabled = false;

            // Footer
            int btnY = this.ClientSize.Height - 70;
            okButton = new Button() { Text = "Smart Optimize", Size = new Size(160, 44), DialogResult = DialogResult.OK, FlatStyle = FlatStyle.Flat, BackColor = Theme.Primary, ForeColor = Color.White, Cursor = Cursors.Hand };
            okButton.Location = new Point(this.ClientSize.Width - 160 - 35, btnY);
            okButton.FlatAppearance.BorderSize = 0;
            okButton.Click += (s, e) => {
                OptimizeBySize = sizeRadio.Checked;
                TargetMiB = (float)mibInput.Value;
                MaxResolution = int.Parse(resCombo.SelectedItem.ToString());
                PreferredFormat = formatCombo.SelectedItem.ToString();
            };

            cancelButton = new Button() { Text = "Cancel", Size = new Size(100, 44), DialogResult = DialogResult.Cancel, FlatStyle = FlatStyle.Flat, ForeColor = Color.Gray, Cursor = Cursors.Hand };
            cancelButton.Location = new Point(okButton.Left - 110, btnY);
            
            this.Controls.Add(okButton);
            this.Controls.Add(cancelButton);
            this.AcceptButton = okButton;
        }
    }
}
