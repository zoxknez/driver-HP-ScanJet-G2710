using G2710.App;
using G2710.Interop;
using Xunit;

namespace G2710.App.Tests;

public sealed class CropTransformTests
{
    private static readonly ScanGeometry Preview = new(100, 80, 8, 3, 300, 75, true);
    private static readonly ScanGeometry Target = new(400, 320, 8, 3, 1200, 300, true);

    [Fact]
    public void Maps_selection_between_resolutions()
    {
        var result = CropTransform.ToScanSettings(new CropTransform.Rect(10, 12, 50, 20), Preview, Target, new ScanSettings());
        Assert.Equal((40, 48, 200, 80), (result.Left, result.Top, result.Width, result.Height));
    }

    [Fact]
    public void Clips_selection_to_image_boundary()
    {
        var result = CropTransform.ToScanSettings(new CropTransform.Rect(-5, 70, 30, 30), Preview, Target, new ScanSettings());
        Assert.Equal((0, 280, 100, 40), (result.Left, result.Top, result.Width, result.Height));
    }

    [Fact]
    public void Empty_selection_means_full_bed()
    {
        var result = CropTransform.ToScanSettings(new CropTransform.Rect(3, 3, 0, 4), Preview, Target,
            new ScanSettings { Resolution = 300, Left = 5, Top = 6, Width = 7, Height = 8 });
        Assert.Equal((0, 0, 0, 0), (result.Left, result.Top, result.Width, result.Height));
    }

    [Fact]
    public void Collapsed_selection_after_clipping_is_rejected()
    {
        Assert.Throws<ArgumentException>(() => CropTransform.ToScanSettings(
            new CropTransform.Rect(-10, 10, 4, 5), Preview, Target, new ScanSettings()));
    }
}
