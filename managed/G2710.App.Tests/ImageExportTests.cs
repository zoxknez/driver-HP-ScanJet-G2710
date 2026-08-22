using System;
using System.IO;
using G2710.App;
using Xunit;

namespace G2710.App.Tests;

public sealed class ImageExportTests : IDisposable
{
    private readonly string _directory = Path.Combine(Path.GetTempPath(), $"g2710-export-{Guid.NewGuid():N}");
    public ImageExportTests() => Directory.CreateDirectory(_directory);

    [Theory]
    [InlineData(ExportFormat.Png, ".png", 137, 80)]
    [InlineData(ExportFormat.Jpeg, ".jpg", 255, 216)]
    [InlineData(ExportFormat.Tiff8, ".tif", 73, 73)]
    public void Exports_8_bit_raster(ExportFormat format, string extension, int first, int second)
    {
        var path = Path.Combine(_directory, "image" + extension);
        ImageExport.Save(Rgb8(), format, path);
        AssertHeader(path, first, second);
    }

    [Fact]
    public void Exports_16_bit_tiff()
    {
        var path = Path.Combine(_directory, "image16.tif");
        ImageExport.Save(new ScanImage(2, 1, 16, 3, [0, 0, 255, 255, 20, 0, 0, 1, 0, 2, 0, 3]), ExportFormat.Tiff16, path);
        AssertHeader(path, 73, 73);
    }

    [Fact]
    public void Exports_multi_page_pdf()
    {
        var path = Path.Combine(_directory, "image.pdf");
        ImageExport.Save(Rgb8(), ExportFormat.Pdf, path, [Rgb8()]);
        var text = File.ReadAllText(path);
        Assert.StartsWith("%PDF-1.4", text, StringComparison.Ordinal);
        Assert.Contains("/Count 2", text, StringComparison.Ordinal);
    }

    [Fact]
    public void Rejects_incomplete_raster()
    {
        Assert.Throws<ArgumentException>(() => ImageExport.Save(new ScanImage(2, 2, 8, 3, [1]), ExportFormat.Png, Path.Combine(_directory, "bad.png")));
    }

    public void Dispose() { if (Directory.Exists(_directory)) Directory.Delete(_directory, true); }
    private static ScanImage Rgb8() => new(2, 1, 8, 3, [255, 0, 0, 0, 255, 0]);
    private static void AssertHeader(string path, int first, int second)
    {
        var bytes = File.ReadAllBytes(path);
        Assert.Equal((byte)first, bytes[0]);
        Assert.Equal((byte)second, bytes[1]);
    }
}
