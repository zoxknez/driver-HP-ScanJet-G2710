using System;
using G2710.App;
using G2710.Interop;
using Xunit;

namespace G2710.App.Tests;

public sealed class ScanCaptureTests
{
    private static ScanGeometry Geometry => new(2, 2, 8, 3, 6, 300, true);

    [Fact]
    public void Produces_image_only_after_every_line()
    {
        var capture = new ScanCapture(Geometry);
        capture.NextLine.Fill(4); capture.CommitLine();
        capture.NextLine.Fill(7); capture.CommitLine();
        var image = capture.Complete();
        Assert.Equal(100, capture.Percent);
        Assert.Equal([4, 4, 4, 4, 4, 4, 7, 7, 7, 7, 7, 7], image.Pixels);
    }

    [Fact]
    public void Rejects_incomplete_capture()
    {
        var capture = new ScanCapture(Geometry);
        capture.CommitLine();
        Assert.Throws<InvalidOperationException>(capture.Complete);
    }

    [Fact]
    public void Rejects_extra_line()
    {
        var capture = new ScanCapture(Geometry);
        capture.CommitLine(); capture.CommitLine();
        Assert.Throws<InvalidOperationException>(() => _ = capture.NextLine);
        Assert.Throws<InvalidOperationException>(capture.CommitLine);
    }
}
