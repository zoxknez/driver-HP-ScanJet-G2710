using System;
using System.IO;
using G2710.App;
using System.Windows.Media;
using System.Windows.Media.Imaging;
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

    // Redosled bajtova na 16 bita.
    //
    // Jezgro isporucuje little-endian - `PnmWriter` bas zato zamenjuje bajtove
    // pre upisa, jer PNM trazi big-endian. WPF-ovi Gray16 i Rgb48 su takodje
    // little-endian, pa je put ispravan. Ali to niko nije DRZAO: postojeci test
    // proverava samo dva magicna bajta TIFF zaglavlja i nikada ne procita
    // nijedan piksel.
    //
    // Zamena redosleda je promena od jedne linije, a posledica je slika u kojoj
    // je svaka vrednost besmislena - i to se ne vidi u testu koji gleda samo
    // zaglavlje.
    [Fact]
    public void Sixteen_bit_samples_survive_the_round_trip_unswapped()
    {
        var path = Path.Combine(_directory, "endianness.tif");

        // Vrednosti kojima se dva bajta RAZLIKUJU - inace zamena ne bi imala
        // vidljivu posledicu.
        const ushort red = 0x1234, green = 0x5678, blue = 0x9ABC;
        byte[] pixels =
        [
            (byte)(red & 0xFF), (byte)(red >> 8),
            (byte)(green & 0xFF), (byte)(green >> 8),
            (byte)(blue & 0xFF), (byte)(blue >> 8),
        ];

        ImageExport.Save(new ScanImage(1, 1, 16, 3, pixels), ExportFormat.Tiff16, path);

        using var stream = File.OpenRead(path);
        var decoder = new TiffBitmapDecoder(stream, BitmapCreateOptions.PreservePixelFormat,
                                            BitmapCacheOption.OnLoad);
        BitmapSource frame = decoder.Frames[0];

        var samples = new ushort[3];
        frame.CopyPixels(samples, 6, 0);

        Assert.Equal(red, samples[0]);
        Assert.Equal(green, samples[1]);
        Assert.Equal(blue, samples[2]);
    }

    [Fact]
    public void Eight_bit_channels_are_not_swapped_on_the_way_out()
    {
        // WPF-ov TIFF dekoder vraca Bgr24 i kada se trazi PreservePixelFormat -
        // to je njegov prirodni oblik u memoriji, ne osobina fajla. Zato se
        // redosled cita IZ prijavljenog formata, a ne pretpostavlja.
        //
        // Prva verzija ovog testa je pretpostavila RGB i pala sa [48,32,16].
        // Fajl je bio ispravan; pogresna je bila pretpostavka.
        var path = Path.Combine(_directory, "channels.tif");
        ImageExport.Save(new ScanImage(1, 1, 8, 3, [0x10, 0x20, 0x30]), ExportFormat.Tiff8, path);

        using var stream = File.OpenRead(path);
        var decoder = new TiffBitmapDecoder(stream, BitmapCreateOptions.PreservePixelFormat,
                                            BitmapCacheOption.OnLoad);
        BitmapSource frame = decoder.Frames[0];

        var samples = new byte[3];
        frame.CopyPixels(samples, 3, 0);

        (byte red, byte green, byte blue) = frame.Format == PixelFormats.Bgr24
            ? (samples[2], samples[1], samples[0])
            : (samples[0], samples[1], samples[2]);

        Assert.Equal(0x10, red);
        Assert.Equal(0x20, green);
        Assert.Equal(0x30, blue);
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
