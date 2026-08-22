using System;
using System.IO;
using G2710.App;
using G2710.Interop;
using Xunit;

namespace G2710.App.Tests;

/// <summary>Pravi ABI + simulator, bez UI mock-a: tok koji koristi desktop app.</summary>
public sealed class SimulatorWorkflowTests
{
    [Fact]
    public void Preview_crop_scan_and_export_complete_over_the_real_native_library()
    {
        using var scanner = Scanner.Open(new ScannerOptions
        {
            Transport = ScannerTransport.Simulator,
            RequestedSafetyLevel = 5,
            ClientName = "app-workflow-test",
        });
        scanner.Identify();
        scanner.Begin();
        try
        {
            scanner.Warmup(TimeSpan.Zero);
            var previewSettings = new ScanSettings
            {
                Resolution = 150, ColorMode = ScanColorMode.Color, BitsPerChannel = 8,
                Width = 64, Height = 8, AllowUnqualified = true,
            };
            var preview = Assert.IsType<ScanImage>(ScanWorkflow.Run(scanner, previewSettings));
            Assert.Equal((64, 8), (preview.Width, preview.Height));

            var target = Scanner.Plan(previewSettings with { Resolution = 300 });
            var previewGeometry = new ScanGeometry(preview.Width, preview.Height, preview.BitsPerChannel,
                preview.Channels, preview.Width * preview.Channels, 150, false);
            var scanSettings = CropTransform.ToScanSettings(
                new CropTransform.Rect(8, 1, 32, 4), previewGeometry, target,
                previewSettings with { Resolution = 300 });
            var image = Assert.IsType<ScanImage>(ScanWorkflow.Run(scanner, scanSettings));
            Assert.True(image.Width > 0);
            Assert.True(image.Height > 0);

            var path = Path.Combine(Path.GetTempPath(), $"g2710-app-flow-{Guid.NewGuid():N}.png");
            try
            {
                ImageExport.Save(image, ExportFormat.Png, path);
                Assert.True(File.Exists(path));
                Assert.True(new FileInfo(path).Length > 0);
            }
            finally { File.Delete(path); }
        }
        finally { scanner.End(); }
    }
}
