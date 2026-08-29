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

    // Odnos koji NIJE ceo broj: 100 -> 150 je 1.5.
    //
    // Postojeci testovi koriste 100 -> 400, dakle tacno cetiri. Pri celom
    // odnosu skaliranje sirine i skaliranje krajnje granice daju isti rezultat,
    // pa je svojstvo koje ovaj kod tvrdi - da susedni izbori ostaju susedni -
    // bilo NEMERENO. Mutaciona provera je to i pokazala: mutacija koja skalira
    // sirinu pala je zbog opsecanja, ne zbog susednosti.
    private static readonly ScanGeometry Odd = new(150, 120, 8, 3, 450, 450, true);

    [Fact]
    public void Neighbouring_selections_stay_neighbours_at_a_fractional_ratio()
    {
        var settings = new ScanSettings();
        var first = CropTransform.ToScanSettings(new CropTransform.Rect(1, 0, 1, 10), Preview, Odd, settings);
        var second = CropTransform.ToScanSettings(new CropTransform.Rect(2, 0, 1, 10), Preview, Odd, settings);

        // Bez rupe i bez preklapanja: gde prvi stane, drugi pocinje.
        Assert.Equal(first.Left + first.Width, second.Left);
    }

    [Fact]
    public void A_row_of_selections_covers_the_target_without_gaps()
    {
        // Deset susednih izbora po celoj sirini preview-a mora pokriti celu
        // ciljnu sirinu, bez ijednog izgubljenog piksela.
        var settings = new ScanSettings();
        int covered = 0;
        int previous = 0;

        for (int i = 0; i < 10; ++i)
        {
            var piece = CropTransform.ToScanSettings(
                new CropTransform.Rect(i * 10, 0, 10, 10), Preview, Odd, settings);

            Assert.Equal(previous, piece.Left);
            previous = piece.Left + piece.Width;
            covered += piece.Width;
        }

        Assert.Equal(Odd.WidthPixels, covered);
        Assert.Equal(Odd.WidthPixels, previous);
    }

    [Fact]
    public void A_selection_past_the_right_edge_is_clipped_not_wrapped()
    {
        // Izbor koji izlazi van prikaza mora stati na ivicu slike; negativna
        // ili prevelika koordinata poslata skeneru je zahtev koji planer odbija
        // tek posle nego sto se glava vec pomeri.
        var result = CropTransform.ToScanSettings(
            new CropTransform.Rect(90, 70, 50, 50), Preview, Target, new ScanSettings());

        Assert.Equal(Target.WidthPixels, result.Left + result.Width);
        Assert.Equal(Target.Lines, result.Top + result.Height);
        Assert.True(result.Left >= 0 && result.Top >= 0);
    }

    [Fact]
    public void The_rest_of_the_request_survives_the_crop()
    {
        // Crop menja SAMO oblast. Rezolucija, rezim i gamma su korisnikov izbor
        // i ne smeju se izgubiti usput.
        var requested = new ScanSettings
        {
            Resolution = 600, ColorMode = ScanColorMode.Gray, BitsPerChannel = 16,
            Gamma = 2.2, AllowUnqualified = true,
        };
        var result = CropTransform.ToScanSettings(
            new CropTransform.Rect(10, 10, 20, 20), Preview, Target, requested);

        Assert.Equal(600, result.Resolution);
        Assert.Equal(ScanColorMode.Gray, result.ColorMode);
        Assert.Equal(16, result.BitsPerChannel);
        Assert.Equal(2.2, result.Gamma);
        Assert.True(result.AllowUnqualified);
    }

    [Fact]
    public void Collapsed_selection_after_clipping_is_rejected()
    {
        Assert.Throws<ArgumentException>(() => CropTransform.ToScanSettings(
            new CropTransform.Rect(-10, 10, 4, 5), Preview, Target, new ScanSettings()));
    }
}
