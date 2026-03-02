using CodeWalker.GameFiles;
using CodeWalker.Utils;
using System;
using System.Drawing;
using System.IO;
using System.Windows.Forms;
using System.Security.Cryptography;
using BCnEncoder.Encoder;
using BCnEncoder.Decoder;
using BCnEncoder.Shared;

namespace EasyOptimizerV
{
    public partial class MainForm : Form
    {
        private FlowLayoutPanel? flowLayoutPanel;
        private StatusStrip? statusStrip;
        private ToolStripStatusLabel? statusLabel;
        private MenuStrip? menuStrip;
        private ToolStrip? toolStrip;
        private System.Collections.Generic.List<YtdFile> loadedYtds = new System.Collections.Generic.List<YtdFile>();
        private System.Collections.Generic.Dictionary<YtdFile, string> ytdFilePaths = new System.Collections.Generic.Dictionary<YtdFile, string>();
        private TextBox? searchTextBox;
        private string currentSearch = "";
        private string currentPreviewRes = "128";
        private System.Collections.Generic.HashSet<YtdFile> expandedYtds = new System.Collections.Generic.HashSet<YtdFile>();
        private System.Collections.Generic.HashSet<string> expandedVirtualFolders = new System.Collections.Generic.HashSet<string>();
        private System.Collections.Generic.Dictionary<string, System.Collections.Generic.List<(Texture, YtdFile)>> duplicateGroups = new System.Collections.Generic.Dictionary<string, System.Collections.Generic.List<(Texture, YtdFile)>>();
        private bool showingDuplicates = false;
        private EncoderEngine selectedEngine = EncoderEngine.BCnEncoder;
        private ComboBox? encoderCombo;

        public MainForm()
        {
            InitializeComponent();
            try
            {
                SetupUI();
            }
            catch (Exception ex)
            {
                MessageBox.Show($"Startup Error: {ex.Message}\n{ex.StackTrace}", "Critical Error", MessageBoxButtons.OK, MessageBoxIcon.Error);
            }
        }

        private void SetupUI()
        {
            this.Controls.Clear();
            this.Text = "EasyOptimizer-V";
            this.Size = new Size(1024, 768);
            Theme.Apply(this);
            this.ShowIcon = false;

            // Root Layout - TableLayout to guarantee no overlap
            TableLayoutPanel mainLayout = new TableLayoutPanel();
            mainLayout.Dock = DockStyle.Fill;
            mainLayout.ColumnCount = 1;
            mainLayout.RowCount = 2;
            mainLayout.RowStyles.Add(new RowStyle(SizeType.Absolute, 60F)); // Header
            mainLayout.RowStyles.Add(new RowStyle(SizeType.Percent, 100F)); // Content
            mainLayout.Margin = new Padding(0);
            mainLayout.Padding = new Padding(0);
            mainLayout.BackColor = Theme.BackgroundDark;

            // HEADER (Row 0)
            TableLayoutPanel headerTable = new TableLayoutPanel();
            headerTable.Dock = DockStyle.Fill;
            headerTable.Margin = new Padding(0);
            headerTable.BackColor = Theme.SurfaceDark;
            headerTable.ColumnCount = 3;
            headerTable.RowCount = 1;

            headerTable.ColumnStyles.Add(new ColumnStyle(SizeType.AutoSize)); // Title
            headerTable.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100F)); // Search
            
            headerTable.Padding = new Padding(16, 0, 16, 0); 

            // 1. Title
            Label lblTitle = new Label();
            lblTitle.Text = "EasyOptimizer-V";
            lblTitle.Font = new Font("Segoe UI", 12F, FontStyle.Bold);
            lblTitle.ForeColor = Theme.TextPrimaryDark;
            lblTitle.AutoSize = true;
            lblTitle.Anchor = AnchorStyles.Left;
            lblTitle.Margin = new Padding(0, 0, 12, 0); 
            headerTable.Controls.Add(lblTitle, 0, 0);

            // 2. Search Box
            Panel searchContainer = new Panel();
            searchContainer.Height = 36;
            searchContainer.BackColor = Theme.BackgroundDark;
            searchContainer.Anchor = AnchorStyles.Left | AnchorStyles.Right | AnchorStyles.Top; 
            searchContainer.Margin = new Padding(0, 12, 0, 0); 
            
            searchTextBox = new TextBox();
            searchTextBox.BorderStyle = BorderStyle.None;
            searchTextBox.BackColor = Theme.BackgroundDark;
            searchTextBox.ForeColor = Theme.TextPrimaryDark;
            searchTextBox.Font = new Font("Segoe UI", 10F);
            searchTextBox.PlaceholderText = "Search textures...";
            
            // Y=9 to center vertically (36 - TextHeight)/2
            searchTextBox.Location = new Point(12, 9);
            searchTextBox.Width = 200; 
            searchTextBox.Anchor = AnchorStyles.Left | AnchorStyles.Right; 
            searchTextBox.TextChanged += (s, e) => { SearchTextBox_TextChanged(s, e); };
            
            searchContainer.Controls.Add(searchTextBox);
            headerTable.Controls.Add(searchContainer, 1, 0);

            // Add Header to Root Layout (Row 0)
            mainLayout.Controls.Add(headerTable, 0, 0);

            // 1. Grid Size / Preview Button (Cycles through Small, Medium, Native)
            string[] gridNames = { "Small", "Medium", "Native" };
            string[] resValues = { "128", "256", "Native" };
            int currentSizeIndex = 1; // Start at Medium
            
            Button btnGridSize = new Button();
            btnGridSize.Text = $"Grid: {gridNames[currentSizeIndex]}";
            btnGridSize.Font = new Font("Segoe UI", 9F, FontStyle.Bold);
            btnGridSize.BackColor = Theme.Primary;
            btnGridSize.ForeColor = Color.White;
            btnGridSize.FlatStyle = FlatStyle.Flat;
            btnGridSize.FlatAppearance.BorderSize = 0;
            btnGridSize.Height = 36;
            btnGridSize.Margin = new Padding(0, 4, 0, 4);
            btnGridSize.Cursor = Cursors.Hand;

            btnGridSize.Click += (s, e) => {
                currentSizeIndex = (currentSizeIndex + 1) % gridNames.Length;
                btnGridSize.Text = $"Grid: {gridNames[currentSizeIndex]}";
                currentPreviewRes = resValues[currentSizeIndex];

                Size newSize = new Size(220, 260);
                if (currentPreviewRes == "128") newSize = new Size(160, 200);
                else if (currentPreviewRes == "Native") newSize = new Size(300, 340);

                if (flowLayoutPanel != null)
                {
                    flowLayoutPanel.SuspendLayout();
                    foreach (Control c in flowLayoutPanel.Controls)
                    {
                        if (c is TextureCard tc) { tc.Size = newSize; tc.Invalidate(); }
                    }
                    flowLayoutPanel.ResumeLayout();
                }
                RenderTextures();
            };

            Button btnOpen = new Button();
            btnOpen.Text = "Import YTD";
            btnOpen.Font = new Font("Segoe UI", 9F, FontStyle.Bold);
            btnOpen.BackColor = Theme.Primary;
            btnOpen.ForeColor = Color.White;
            btnOpen.FlatStyle = FlatStyle.Flat;
            btnOpen.FlatAppearance.BorderSize = 0;
            btnOpen.Height = 36;
            btnOpen.Margin = new Padding(0, 4, 0, 4);
            btnOpen.Cursor = Cursors.Hand;
            btnOpen.Click += (s, e) => OpenFile_Click(s, e);

            Button btnFolder = new Button();
            btnFolder.Text = "Import Folder";
            btnFolder.Font = new Font("Segoe UI", 9F, FontStyle.Bold);
            btnFolder.BackColor = Theme.Primary;
            btnFolder.ForeColor = Color.White;
            btnFolder.FlatStyle = FlatStyle.Flat;
            btnFolder.FlatAppearance.BorderSize = 0;
            btnFolder.Height = 36;
            btnFolder.Margin = new Padding(0, 4, 0, 4);
            btnFolder.Cursor = Cursors.Hand;
            btnFolder.Click += (s, e) => ImportFolder_Click();

            Button btnClear = new Button();
            btnClear.Text = "Clear All";
            btnClear.Font = new Font("Segoe UI", 9F, FontStyle.Bold);
            btnClear.BackColor = Color.FromArgb(180, 60, 60);
            btnClear.ForeColor = Color.White;
            btnClear.FlatStyle = FlatStyle.Flat;
            btnClear.FlatAppearance.BorderSize = 0;
            btnClear.Height = 36;
            btnClear.Margin = new Padding(0, 4, 0, 4);
            btnClear.Cursor = Cursors.Hand;
            btnClear.Click += (s, e) => ClearAll_Click();

            Button btnSaveAll = new Button();
            btnSaveAll.Text = "Save All";
            btnSaveAll.Font = new Font("Segoe UI", 9F, FontStyle.Bold);
            btnSaveAll.BackColor = Theme.Primary;
            btnSaveAll.ForeColor = Color.White;
            btnSaveAll.FlatStyle = FlatStyle.Flat;
            btnSaveAll.FlatAppearance.BorderSize = 0;
            btnSaveAll.Height = 36;
            btnSaveAll.Margin = new Padding(0, 4, 0, 4);
            btnSaveAll.Cursor = Cursors.Hand;
            btnSaveAll.Click += (s, e) => SaveAll_Click();

            Button btnNameDup = new Button();
            btnNameDup.Text = "Detect Names";
            btnNameDup.Font = new Font("Segoe UI", 9F, FontStyle.Bold);
            btnNameDup.BackColor = Color.FromArgb(70, 70, 70);
            btnNameDup.ForeColor = Color.White;
            btnNameDup.FlatStyle = FlatStyle.Flat;
            btnNameDup.FlatAppearance.BorderSize = 0;
            btnNameDup.Height = 36;
            btnNameDup.Margin = new Padding(0, 4, 0, 4);
            btnNameDup.Cursor = Cursors.Hand;
            btnNameDup.Click += (s, e) => PerformDeDuplicationAnalysis(true, false);

            Button btnHexDup = new Button();
            btnHexDup.Text = "Detect Hex";
            btnHexDup.Font = new Font("Segoe UI", 9F, FontStyle.Bold);
            btnHexDup.BackColor = Color.FromArgb(70, 70, 70);
            btnHexDup.ForeColor = Color.White;
            btnHexDup.FlatStyle = FlatStyle.Flat;
            btnHexDup.FlatAppearance.BorderSize = 0;
            btnHexDup.Height = 36;
            btnHexDup.Margin = new Padding(0, 4, 0, 4);
            btnHexDup.Cursor = Cursors.Hand;
            btnHexDup.Click += (s, e) => PerformDeDuplicationAnalysis(false, true);

            Button btnMigrate = new Button();
            btnMigrate.Text = "Migrate Duplicates";
            btnMigrate.Font = new Font("Segoe UI", 9F, FontStyle.Bold);
            btnMigrate.BackColor = Color.FromArgb(0, 122, 204);
            btnMigrate.ForeColor = Color.White;
            btnMigrate.FlatStyle = FlatStyle.Flat;
            btnMigrate.FlatAppearance.BorderSize = 0;
            btnMigrate.Height = 36;
            btnMigrate.Margin = new Padding(0, 4, 0, 4);
            btnMigrate.Cursor = Cursors.Hand;
            btnMigrate.Click += (s, e) => MigrateDuplicates_Click();

            // CONTENT AREA (Row 1)
            TableLayoutPanel contentLayout = new TableLayoutPanel();
            contentLayout.Dock = DockStyle.Fill;
            contentLayout.ColumnCount = 2;
            contentLayout.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute, 220F)); // Sidebar
            contentLayout.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100F)); // Flow Grid
            contentLayout.Margin = new Padding(0);

            // SIDEBAR
            FlowLayoutPanel sidebar = new FlowLayoutPanel();
            sidebar.Dock = DockStyle.Fill;
            sidebar.BackColor = Theme.SurfaceDark;
            sidebar.FlowDirection = FlowDirection.TopDown;
            sidebar.WrapContents = false;
            sidebar.Padding = new Padding(16);
            sidebar.AutoScroll = true;

            void AddSidebarLabel(string text)
            {
                Label lbl = new Label();
                lbl.Text = text;
                lbl.ForeColor = Theme.TextSecondaryDark;
                lbl.Font = new Font("Segoe UI", 9F, FontStyle.Bold);
                lbl.Margin = new Padding(0, 8, 0, 4);
                lbl.AutoSize = true;
                sidebar.Controls.Add(lbl);
            }

            AddSidebarLabel("VIEW CONTROLS");
            sidebar.Controls.Add(btnGridSize);
            btnGridSize.Width = 188; // Fill sidebar width minus padding

            AddSidebarLabel("IMPORT");
            btnOpen.Width = 188;
            btnFolder.Width = 188;
            sidebar.Controls.Add(btnOpen);
            sidebar.Controls.Add(btnFolder);

            AddSidebarLabel("OPTIMIZATION");
            btnSaveAll.Width = 188;
            btnClear.Width = 188;
            sidebar.Controls.Add(btnSaveAll);
            sidebar.Controls.Add(btnClear);

            AddSidebarLabel("DUPLICATE ANALYSIS");
            btnNameDup.Width = 188;
            btnHexDup.Width = 188;
            btnMigrate.Width = 188;
            sidebar.Controls.Add(btnNameDup);
            sidebar.Controls.Add(btnHexDup);
            sidebar.Controls.Add(btnMigrate);

            AddSidebarLabel("ENCODER ENGINE");
            encoderCombo = new ComboBox();
            encoderCombo.Width = 188;
            encoderCombo.DataSource = Enum.GetValues(typeof(EncoderEngine));
            encoderCombo.SelectedItem = selectedEngine;
            encoderCombo.DropDownStyle = ComboBoxStyle.DropDownList;
            encoderCombo.BackColor = Theme.BackgroundDark;
            encoderCombo.ForeColor = Color.White;
            encoderCombo.FlatStyle = FlatStyle.Flat;
            encoderCombo.SelectedIndexChanged += (s, e) => {
                if (encoderCombo.SelectedItem is EncoderEngine engine)
                    selectedEngine = engine;
            };
            sidebar.Controls.Add(encoderCombo);

            contentLayout.Controls.Add(sidebar, 0, 0);

            // FLOW GRID (Vertical stack for Folders)
            flowLayoutPanel = new FlowLayoutPanel();
            flowLayoutPanel.Dock = DockStyle.Fill;
            flowLayoutPanel.AutoScroll = true;
            flowLayoutPanel.BackColor = Theme.BackgroundDark;
            flowLayoutPanel.FlowDirection = FlowDirection.TopDown; 
            flowLayoutPanel.WrapContents = false; 
            flowLayoutPanel.Padding = new Padding(16);

            // Force folders to 100% width on resize
            flowLayoutPanel.SizeChanged += (s, e) => {
                if (flowLayoutPanel.ClientSize.Width <= 0) return;
                int targetWidth = flowLayoutPanel.ClientSize.Width - flowLayoutPanel.Padding.Horizontal - 25;
                flowLayoutPanel.SuspendLayout();
                foreach(Control c in flowLayoutPanel.Controls) {
                    if (c is YtdFolderCard folder) folder.Width = targetWidth;
                }
                flowLayoutPanel.ResumeLayout();
            };
            
            contentLayout.Controls.Add(flowLayoutPanel, 1, 0);
            
            mainLayout.Controls.Add(contentLayout, 0, 1);
            
            this.Controls.Add(mainLayout);

            // Status Strip (Hidden but initialized)
            statusStrip = new StatusStrip();
            statusStrip.Visible = false;
            statusLabel = new ToolStripStatusLabel();
            statusStrip.Items.Add(statusLabel);
            this.Controls.Add(statusStrip);
        }

        private void SearchTextBox_TextChanged(object? sender, EventArgs e)
        {
            currentSearch = searchTextBox?.Text?.ToLowerInvariant() ?? "";
            RenderTextures();
        }

        private void OpenFile_Click(object? sender, EventArgs e)
        {
            using (OpenFileDialog ofd = new OpenFileDialog())
            {
                ofd.Filter = "YTD Files (*.ytd)|*.ytd|All Files (*.*)|*.*";
                ofd.Multiselect = true;
                if (ofd.ShowDialog() == DialogResult.OK)
                {
                    foreach (string file in ofd.FileNames)
                    {
                        AddYtd(file);
                    }
                    RenderTextures();
                }
            }
        }

        private void ImportFolder_Click()
        {
            using (FolderBrowserDialog fbd = new FolderBrowserDialog())
            {
                fbd.Description = "Select folder to import YTD files recursively";
                fbd.UseDescriptionForTitle = true;
                if (fbd.ShowDialog() == DialogResult.OK)
                {
                    try
                    {
                        string[] files = Directory.GetFiles(fbd.SelectedPath, "*.ytd", SearchOption.AllDirectories);
                        if (files.Length == 0)
                        {
                            MessageBox.Show("No YTD files found in the selected folder.", "Import Folder", MessageBoxButtons.OK, MessageBoxIcon.Information);
                            return;
                        }

                        if (statusLabel != null) statusLabel.Text = $"Importing {files.Length} files...";
                        Application.DoEvents();

                        foreach (string file in files)
                        {
                            AddYtd(file);
                        }
                        RenderTextures();
                        
                        MessageBox.Show($"Successfully imported {files.Length} YTD files.", "Import Folder", MessageBoxButtons.OK, MessageBoxIcon.Information);
                    }
                    catch (Exception ex)
                    {
                        MessageBox.Show($"Error importing folder: {ex.Message}", "Error", MessageBoxButtons.OK, MessageBoxIcon.Error);
                    }
                }
            }
        }

        private void ClearAll_Click()
        {
            loadedYtds.Clear();
            ytdFilePaths.Clear();
            expandedYtds.Clear();
            expandedVirtualFolders.Clear();
            duplicateGroups.Clear();
            showingDuplicates = false;
            RenderTextures();
            if (statusLabel != null) statusLabel.Text = "Cleared all files.";
        }

        private void SaveAll_Click()
        {
            if (loadedYtds.Count == 0) return;
            
            int savedCount = 0;
            try
            {
                foreach (var ytd in loadedYtds)
                {
                    if (ytdFilePaths.TryGetValue(ytd, out string? path))
                    {
                        File.WriteAllBytes(path, ytd.Save());
                        savedCount++;
                    }
                }
                MessageBox.Show($"Successfully saved {savedCount} YTD files.", "Save All", MessageBoxButtons.OK, MessageBoxIcon.Information);
                if (statusLabel != null) statusLabel.Text = $"Saved {savedCount} files.";
            }
            catch (Exception ex)
            {
                MessageBox.Show($"Error saving files: {ex.Message}", "Error", MessageBoxButtons.OK, MessageBoxIcon.Error);
            }
        }

        private void AddYtd(string filename)
        {
            try
            {
                if (statusLabel != null) statusLabel.Text = $"Loading {Path.GetFileName(filename)}...";
                Application.DoEvents();

                byte[] data = File.ReadAllBytes(filename);
                YtdFile ytd = new YtdFile();
                ytd.Load(data);
                ytd.Name = Path.GetFileName(filename);

                loadedYtds.Add(ytd);
                ytdFilePaths[ytd] = filename;

                int count = ytd.TextureDict?.Textures?.data_items?.Length ?? 0;
                if (statusLabel != null) statusLabel.Text = $"Loaded {count} textures from {ytd.Name}";
                
                duplicateGroups.Clear();
                showingDuplicates = false;
                expandedVirtualFolders.Clear();
                RenderTextures(); // Refresh to show the new folder
            }
            catch (Exception ex)
            {
                MessageBox.Show($"Error loading YTD {filename}: {ex.Message}", "Error", MessageBoxButtons.OK, MessageBoxIcon.Error);
            }
        }

        private void RenderTextures()
        {
            if (flowLayoutPanel == null) return;
            flowLayoutPanel.SuspendLayout();
            
            int scrollY = flowLayoutPanel.VerticalScroll.Value;
            flowLayoutPanel.Controls.Clear();

            int targetWidth = flowLayoutPanel.ClientSize.Width - flowLayoutPanel.Padding.Horizontal - 25;
            if (targetWidth < 200) targetWidth = 800; // Fallback

            int totalVisibleTextures = 0;

            // 1. Render DUPLICATES folder if active
            if (showingDuplicates && duplicateGroups.Count > 0)
            {
                bool rootExpanded = expandedVirtualFolders.Contains("DUPLICATES_ROOT");
                var rootFolder = new YtdFolderCard("DUPLICATES_ROOT", "DUPLICATES (FOLDER)", $"{duplicateGroups.Count} unique groups detected", Color.Cyan, rootExpanded, targetWidth);
                rootFolder.OnToggleRequested += (card) => {
                    if (expandedVirtualFolders.Contains(card.VirtualId!)) expandedVirtualFolders.Remove(card.VirtualId!);
                    else expandedVirtualFolders.Add(card.VirtualId!);
                    RenderTextures();
                };
                flowLayoutPanel.Controls.Add(rootFolder);

                if (rootExpanded)
                {
                    foreach (var kvp in duplicateGroups)
                    {
                        string groupId = $"DUP_GROUP:{kvp.Key}";
                        bool groupExpanded = expandedVirtualFolders.Contains(groupId);
                        
                        // Use the name of the first texture in the group
                        string displayName = kvp.Value.Count > 0 ? kvp.Value[0].Item1.Name : "Unknown Group";
                        
                        // Sub-folder per duplicate group (indented)
                        var groupCard = new YtdFolderCard(groupId, $"Group: {displayName}", $"{kvp.Value.Count} instances", Color.Yellow, groupExpanded, targetWidth - 20);
                        groupCard.Margin = new Padding(20, 0, 0, 4);
                        groupCard.OnToggleRequested += (card) => {
                            if (expandedVirtualFolders.Contains(card.VirtualId!)) expandedVirtualFolders.Remove(card.VirtualId!);
                            else expandedVirtualFolders.Add(card.VirtualId!);
                            RenderTextures();
                        };
                        flowLayoutPanel.Controls.Add(groupCard);

                        if (groupExpanded)
                        {
                            FlowLayoutPanel subGrid = new FlowLayoutPanel();
                            subGrid.FlowDirection = FlowDirection.LeftToRight;
                            subGrid.WrapContents = true;
                            subGrid.AutoSize = true;
                            subGrid.Width = targetWidth;
                            subGrid.Padding = new Padding(40, 12, 0, 24); // More indent
                            subGrid.BackColor = Color.Transparent;

                            foreach (var item in kvp.Value)
                            {
                                AddTextureToGrid(item.Item1, item.Item2, subGrid);
                                totalVisibleTextures++;
                            }
                            flowLayoutPanel.Controls.Add(subGrid);
                        }
                    }
                }
            }

            // 2. Render normal YTD Folders
            foreach (var ytd in loadedYtds)
            {
                // Add Folder Header
                bool isExpanded = expandedYtds.Contains(ytd);
                var folderCard = new YtdFolderCard(ytd, isExpanded, targetWidth);
                folderCard.OnToggleRequested += (card) => {
                    bool wasExpanded = expandedYtds.Contains(card.Ytd!);
                    expandedYtds.Clear(); // Accordion for YTDs
                    if (!wasExpanded) expandedYtds.Add(card.Ytd!);
                    RenderTextures();
                };
                flowLayoutPanel.Controls.Add(folderCard);

                // If expanded, add sub-grid of textures
                if (isExpanded && ytd.TextureDict?.Textures?.data_items != null)
                {
                    var texturesInYtd = new System.Collections.Generic.List<CodeWalker.GameFiles.Texture>();
                    foreach (var tex in ytd.TextureDict.Textures.data_items)
                    {
                        if (string.IsNullOrEmpty(currentSearch) || tex.Name.ToLowerInvariant().Contains(currentSearch))
                        {
                            texturesInYtd.Add(tex);
                        }
                    }

                    texturesInYtd.Sort((a, b) => string.Compare(a.Name, b.Name, StringComparison.OrdinalIgnoreCase));
                    totalVisibleTextures += texturesInYtd.Count;

                    if (texturesInYtd.Count > 0)
                    {
                        // Sub-grid for textures
                        FlowLayoutPanel subGrid = new FlowLayoutPanel();
                        subGrid.FlowDirection = FlowDirection.LeftToRight;
                        subGrid.WrapContents = true;
                        subGrid.AutoSize = true;
                        subGrid.Width = targetWidth;
                        subGrid.Padding = new Padding(24, 12, 0, 24); // Indent textures
                        subGrid.BackColor = Color.Transparent;

                        foreach (var tex in texturesInYtd)
                        {
                            AddTextureToGrid(tex, ytd, subGrid);
                        }
                        flowLayoutPanel.Controls.Add(subGrid);
                    }
                }
            }
            
            if (statusLabel != null)
            {
               statusLabel.Text = $"Loaded {loadedYtds.Count} YTDs. Showing {totalVisibleTextures} textures.";
            }

            flowLayoutPanel.ResumeLayout();
            try { flowLayoutPanel.VerticalScroll.Value = Math.Min(scrollY, flowLayoutPanel.VerticalScroll.Maximum); } catch {}
        }

        private void AddTextureToGrid(Texture tex, YtdFile parent, FlowLayoutPanel targetGrid)
        {
            try
            {
                int previewSize = 128;
                bool nativeSize = false;
                if (currentPreviewRes == "Native") nativeSize = true;
                else int.TryParse(currentPreviewRes, out previewSize);

                // Get pixels (mip 0)
                byte[] pixels = DDSIO.GetPixels(tex, 0);
                if (pixels == null) return;

                int width = tex.Width;
                int height = tex.Height;

                // Create Original Bitmap
                using (Bitmap originalBmp = new Bitmap(width, height, System.Drawing.Imaging.PixelFormat.Format32bppArgb))
                {
                    var data = originalBmp.LockBits(new Rectangle(0, 0, width, height), System.Drawing.Imaging.ImageLockMode.WriteOnly, originalBmp.PixelFormat);
                    System.Runtime.InteropServices.Marshal.Copy(pixels, 0, data.Scan0, pixels.Length);
                    originalBmp.UnlockBits(data);

                    int displayW = nativeSize ? width : previewSize;
                    int displayH = nativeSize ? height : previewSize;
                    
                    // Maintain aspect ratio if not native
                    if (!nativeSize)
                    {
                        float ratio = (float)width / height;
                        if (ratio > 1) displayH = (int)(displayW / ratio);
                        else displayW = (int)(displayH * ratio);
                    }

                    Bitmap displayBmp = new Bitmap(originalBmp, displayW, displayH);

                    // Create UI Component: Modern Texture Card
                    var card = new TextureCard(tex, displayBmp, parent);
                    card.OnResizeRequested += (t) => ResizeTexture_Click(t, parent);
                    
                    // Set current size based on global state
                    Size newSize = new Size(220, 260); 
                    if (currentPreviewRes == "128") newSize = new Size(160, 200);
                    else if (currentPreviewRes == "Native") newSize = new Size(300, 340);
                    card.Size = newSize;

                    // Context Menu
                    var ctx = new ContextMenuStrip();
                    var darkRenderer = new ToolStripProfessionalRenderer(new DarkColorTable());
                    ctx.Renderer = darkRenderer;
                    ctx.BackColor = Color.FromArgb(45, 45, 48);
                    ctx.ForeColor = Color.White;
                    var resizeItem = new ToolStripMenuItem("Resize...", null, (s, e) => ResizeTexture_Click(tex, parent));
                    resizeItem.ForeColor = Color.White;
                    ctx.Items.Add(resizeItem);
                    
                    card.ContextMenuStrip = ctx;
                    targetGrid.Controls.Add(card);
                }
            }
            catch (Exception ex)
            {
                // Silent fail for single texture load error to keep UI moving
            }
        }

        private void ResizeTexture_Click(Texture tex, YtdFile parent)
        {
            using (var dialog = new ResizeDialog(tex.Width, tex.Height))
            {
                if (dialog.ShowDialog() == DialogResult.OK)
                {
                    PerformTextureResize(tex, parent, dialog.NewWidth, dialog.NewHeight, dialog.SelectedFormat, dialog.NewMips);
                }
            }
        }

        private void PerformTextureResize(Texture tex, YtdFile parent, int newWidth, int newHeight, string formatSelection, int desiredMips)
        {
            try
            {
                // 1. Decode Texture
                byte[] rawBgraPixels = null;
                try {
                    CompressionFormat? inputFormat = null;
                    if (tex.Format == TextureFormat.D3DFMT_DXT1) inputFormat = CompressionFormat.Bc1;
                    else if (tex.Format == TextureFormat.D3DFMT_DXT3) inputFormat = CompressionFormat.Bc2;
                    else if (tex.Format == TextureFormat.D3DFMT_DXT5) inputFormat = CompressionFormat.Bc3;
                    else if (tex.Format == TextureFormat.D3DFMT_ATI1) inputFormat = CompressionFormat.Bc4;
                    else if (tex.Format == TextureFormat.D3DFMT_ATI2) inputFormat = CompressionFormat.Bc5;
                    else if (tex.Format.ToString().Contains("BC7")) inputFormat = CompressionFormat.Bc7;

                    if (inputFormat.HasValue && tex.Data?.FullData != null) {
                        var decoder = new BcDecoder();
                        var decodedColors = decoder.DecodeRaw(tex.Data.FullData, tex.Width, tex.Height, inputFormat.Value);
                        if (decodedColors != null) {
                            rawBgraPixels = new byte[decodedColors.Length * 4];
                            for (int i = 0; i < decodedColors.Length; i++) {
                                var color = decodedColors[i];
                                int offset = i * 4;
                                rawBgraPixels[offset] = color.b;
                                rawBgraPixels[offset + 1] = color.g;
                                rawBgraPixels[offset + 2] = color.r;
                                rawBgraPixels[offset + 3] = color.a;
                            }
                        }
                    }
                } catch { }

                if (rawBgraPixels == null) {
                    byte[] cwPixels = DDSIO.GetPixels(tex, 0);
                    if (cwPixels != null) rawBgraPixels = cwPixels;
                }

                if (rawBgraPixels == null) throw new Exception("Could not decode texture.");

                // 2. Determine Original Formats
                TextureFormat targetFormat = Mapping.ParseFormat(formatSelection, tex.Format);

                // 3. Create Resized Bitmap
                using (Bitmap fullBmp = new Bitmap(tex.Width, tex.Height, System.Drawing.Imaging.PixelFormat.Format32bppArgb))
                {
                    var bmpData = fullBmp.LockBits(new Rectangle(0, 0, tex.Width, tex.Height), System.Drawing.Imaging.ImageLockMode.WriteOnly, fullBmp.PixelFormat);
                    System.Runtime.InteropServices.Marshal.Copy(rawBgraPixels, 0, bmpData.Scan0, Math.Min(rawBgraPixels.Length, bmpData.Stride * tex.Height));
                    fullBmp.UnlockBits(bmpData);

                    using (Bitmap resizedBmp = new Bitmap(newWidth, newHeight, System.Drawing.Imaging.PixelFormat.Format32bppArgb))
                    {
                        using (Graphics g = Graphics.FromImage(resizedBmp)) {
                            g.InterpolationMode = System.Drawing.Drawing2D.InterpolationMode.HighQualityBicubic;
                            g.DrawImage(fullBmp, 0, 0, newWidth, newHeight);
                        }

                        // 4. Encode using selected engine
                        int mipsGenerated = 0;
                        int mipLimit = desiredMips == -2 ? tex.Levels : (desiredMips == -1 ? 99 : (desiredMips == 0 ? 1 : desiredMips));
                        
                        var encoder = EncoderManager.GetEncoder(selectedEngine);
                        byte[] fullData = encoder.Encode(resizedBmp, targetFormat, mipLimit, out mipsGenerated);

                        // 5. Update Texture
                        int oldSize = tex.Data?.FullData?.Length ?? 0;
                        tex.Width = (ushort)newWidth;
                        tex.Height = (ushort)newHeight;
                        tex.Levels = (byte)mipsGenerated;
                        tex.Format = targetFormat;
                        
                        if (tex.Data == null) tex.Data = new TextureData();
                        tex.Data.FullData = fullData;

                        // Stride calculation
                        if (Mapping.IsCompressed(targetFormat)) {
                            int blocksWide = Math.Max(1, (newWidth + 3) / 4);
                            int blockSize = (targetFormat == TextureFormat.D3DFMT_DXT1 || targetFormat == TextureFormat.D3DFMT_ATI1) ? 8 : 16;
                            tex.Stride = (ushort)(blocksWide * blockSize);
                        } else {
                            int bpp = (targetFormat == TextureFormat.D3DFMT_A1R5G5B5) ? 2 : ((targetFormat == TextureFormat.D3DFMT_A8) ? 1 : 4);
                            tex.Stride = (ushort)(newWidth * bpp);
                        }

                        RenderTextures();
                        MessageBox.Show($"Resized to {newWidth}x{newHeight} using {selectedEngine}.\nFormat: {targetFormat}, Mips: {mipsGenerated}\nSize: {oldSize:N0} -> {fullData.Length:N0} bytes");
                    }
                }
            }
            catch (Exception ex)
            {
                MessageBox.Show($"Error resizing texture: {ex.Message}");
            }
        }
        private void PerformDeDuplicationAnalysis(bool byName, bool byHex)
        {
            if (loadedYtds.Count == 0) return;
            
            duplicateGroups.Clear();
            var allTextures = new System.Collections.Generic.List<(Texture tex, YtdFile ytd)>();
            
            foreach (var ytd in loadedYtds)
            {
                if (ytd.TextureDict?.Textures?.data_items != null)
                {
                    foreach (var tex in ytd.TextureDict.Textures.data_items)
                        allTextures.Add((tex, ytd));
                }
            }

            var groups = new System.Collections.Generic.Dictionary<string, System.Collections.Generic.List<(Texture, YtdFile)>>(StringComparer.OrdinalIgnoreCase);

            foreach (var item in allTextures)
            {
                string key = "";
                if (byName) key = item.tex.Name;
                else if (byHex) key = GetTextureHash(item.tex);

                if (string.IsNullOrEmpty(key)) continue;

                if (!groups.ContainsKey(key))
                    groups[key] = new System.Collections.Generic.List<(Texture, YtdFile)>();
                
                groups[key].Add(item);
            }

            int dupCount = 0;
            foreach (var kvp in groups)
            {
                if (kvp.Value.Count > 1)
                {
                    duplicateGroups[kvp.Key] = kvp.Value;
                    dupCount++;
                }
            }

            showingDuplicates = true;
            expandedYtds.Clear();
            RenderTextures();

            string modeStr = byHex ? "Hex (Conteúdo)" : "Nome";
            MessageBox.Show($"Análise concluída usando matching por {modeStr}.\nEncontrados {dupCount} grupos de duplicatas.", "Análise de Duplicatas", MessageBoxButtons.OK, MessageBoxIcon.Information);
            if (statusLabel != null) statusLabel.Text = $"Encontrados {dupCount} grupos duplicados ({modeStr})";
        }

        private string GetTextureHash(Texture tex)
        {
            if (tex.Data?.FullData == null) return "";
            using (SHA256 sha256 = SHA256.Create())
            {
                byte[] hashBytes = sha256.ComputeHash(tex.Data.FullData);
                return BitConverter.ToString(hashBytes).Replace("-", "").ToLower();
            }
        }

        private void MigrateDuplicates_Click()
        {
            if (duplicateGroups.Count == 0)
            {
                MessageBox.Show("Please run Duplicate Analysis first and ensure duplicates were found.", "Migrate Duplicates", MessageBoxButtons.OK, MessageBoxIcon.Warning);
                return;
            }

            var confirm = MessageBox.Show($"This will move one instance of each duplicate group into a new YTD and REMOVE them from their original files.\n\nGroups to migrate: {duplicateGroups.Count}\n\nDo you want to proceed?", "Confirm Migration", MessageBoxButtons.YesNo, MessageBoxIcon.Question);
            if (confirm != DialogResult.Yes) return;

            try
            {
                // 1. Create the Consolidated YTD with proper structure
                YtdFile consolidatedYtd = new YtdFile();
                consolidatedYtd.Name = "consolidated_textures.ytd";
                consolidatedYtd.TextureDict = new TextureDictionary();
                consolidatedYtd.TextureDict.Textures = new ResourcePointerList64<Texture>();
                consolidatedYtd.TextureDict.TextureNameHashes = new ResourceSimpleList64_uint();
                
                var newTextureList = new System.Collections.Generic.List<Texture>();
                var newHashList = new System.Collections.Generic.List<uint>();

                // 2. Process groups
                foreach (var kvp in duplicateGroups)
                {
                    if (kvp.Value == null || kvp.Value.Count == 0) continue;

                    // Take the first one as master safely
                    var masterItem = kvp.Value[0];
                    if (masterItem.Item1 == null) continue;
                    
                    newTextureList.Add(masterItem.Item1);
                    newHashList.Add(masterItem.Item1.NameHash);

                    // Remove from all originals
                    foreach (var occurrence in kvp.Value)
                    {
                        var sourceYtd = occurrence.Item2;
                        var sourceTex = occurrence.Item1;

                        if (sourceYtd?.TextureDict?.Textures?.data_items != null)
                        {
                            var list = new System.Collections.Generic.List<Texture>(sourceYtd.TextureDict.Textures.data_items);
                            list.Remove(sourceTex);
                            sourceYtd.TextureDict.Textures.data_items = list.ToArray();
                            
                            // Also update hashes if present
                            if (sourceYtd.TextureDict.TextureNameHashes?.data_items != null)
                            {
                                var hlist = new System.Collections.Generic.List<uint>(sourceYtd.TextureDict.TextureNameHashes.data_items);
                                hlist.Remove(sourceTex.NameHash);
                                sourceYtd.TextureDict.TextureNameHashes.data_items = hlist.ToArray();
                            }
                        }
                    }
                }

                consolidatedYtd.TextureDict.Textures.data_items = newTextureList.ToArray();
                consolidatedYtd.TextureDict.TextureNameHashes.data_items = newHashList.ToArray();

                // 3. Save the new file
                SaveFileDialog sfd = new SaveFileDialog();
                sfd.FileName = "consolidated_textures.ytd";
                sfd.Filter = "YTD Files (*.ytd)|*.ytd";
                if (sfd.ShowDialog() == DialogResult.OK)
                {
                    byte[] savedData = consolidatedYtd.Save();
                    if (savedData == null) throw new Exception("Failed to generate YTD data (Save returned null).");
                    
                    File.WriteAllBytes(sfd.FileName, savedData);
                    
                    // Add it to our session too
                    loadedYtds.Add(consolidatedYtd);
                    ytdFilePaths[consolidatedYtd] = sfd.FileName;

                    MessageBox.Show("Migration complete! Duplicate textures have been consolidated and original files optimized.", "Success", MessageBoxButtons.OK, MessageBoxIcon.Information);
                    
                    duplicateGroups.Clear();
                    showingDuplicates = false;
                    expandedVirtualFolders.Clear();
                    RenderTextures();
                }
            }
            catch (Exception ex)
            {
                MessageBox.Show($"Error during migration: {ex.Message}\n\nStack Trace: {ex.StackTrace}", "Error", MessageBoxButtons.OK, MessageBoxIcon.Error);
            }
        }
    }
}
