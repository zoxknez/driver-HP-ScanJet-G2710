using System;
using System.Collections.Generic;
using System.IO;
using System.IO.Compression;
using System.Linq;
using System.Text;
using System.Windows.Media;
using System.Windows.Media.Imaging;

namespace G2710.App;

public enum ExportFormat { Png, Jpeg, Tiff8, Tiff16, Pdf }

/// <summary>Celovita slika dobijena iz ScanSession-a. Ne predstavlja delimičan ili otkazan prolaz.</summary>
public sealed record ScanImage(int Width, int Height, int BitsPerChannel, int Channels, byte[] Pixels)
{
    public int BytesPerPixel => checked(Channels * BitsPerChannel / 8);
    public int Stride => checked(Width * BytesPerPixel);

    public void Validate()
    {
        if (Width <= 0 || Height <= 0 || Channels is < 1 or > 3 || BitsPerChannel is not (8 or 16))
            throw new ArgumentException("Nepodržana geometrija slike.");
        if (Pixels.Length != checked(Stride * Height))
            throw new ArgumentException("Veličina piksela se ne slaže sa geometrijom slike.");
    }
}

/// <summary>Izvoz slika. WPF encoderi vode računa o JPEG/PNG/TIFF detaljima; PDF je mali samostalni writer.</summary>
public static class ImageExport
{
    public static void Save(ScanImage image, ExportFormat format, string path, IReadOnlyList<ScanImage>? additionalPages = null)
    {
        ArgumentNullException.ThrowIfNull(image);
        ArgumentException.ThrowIfNullOrWhiteSpace(path);
        image.Validate();
        if (format == ExportFormat.Pdf)
        {
            SavePdf(Join(image, additionalPages), path);
            return;
        }
        if (additionalPages is { Count: > 0 }) throw new ArgumentException("Više stranica podržava samo PDF.", nameof(additionalPages));
        BitmapEncoder encoder = format switch
        {
            ExportFormat.Png => new PngBitmapEncoder(),
            ExportFormat.Jpeg when image.BitsPerChannel == 8 => new JpegBitmapEncoder { QualityLevel = 92 },
            ExportFormat.Tiff8 when image.BitsPerChannel == 8 => new TiffBitmapEncoder(),
            ExportFormat.Tiff16 when image.BitsPerChannel == 16 => new TiffBitmapEncoder(),
            ExportFormat.Jpeg => throw new ArgumentException("JPEG ne podržava 16-bitni ulaz."),
            ExportFormat.Tiff8 => throw new ArgumentException("TIFF 8 zahteva 8-bitni ulaz."),
            ExportFormat.Tiff16 => throw new ArgumentException("TIFF 16 zahteva 16-bitni ulaz."),
            _ => throw new ArgumentOutOfRangeException(nameof(format)),
        };
        encoder.Frames.Add(BitmapFrame.Create(CreateBitmap(image)));
        using var file = File.Create(path);
        encoder.Save(file);
    }

    /// <summary>WPF prikaz iste slike koja ide u izvoz.</summary>
    public static BitmapSource CreateBitmap(ScanImage image)
    {
        var format = (image.Channels, image.BitsPerChannel) switch
        {
            (1, 8) => PixelFormats.Gray8,
            (3, 8) => PixelFormats.Rgb24,
            (1, 16) => PixelFormats.Gray16,
            (3, 16) => PixelFormats.Rgb48,
            _ => throw new ArgumentException("Kombinacija kanala i dubine nije podržana."),
        };
        return BitmapSource.Create(image.Width, image.Height, 300, 300, format, null, image.Pixels, image.Stride);
    }

    private static IReadOnlyList<ScanImage> Join(ScanImage first, IReadOnlyList<ScanImage>? rest)
    {
        var all = new List<ScanImage> { first };
        if (rest is not null) all.AddRange(rest);
        foreach (var image in all) { image.Validate(); if (image.BitsPerChannel != 8) throw new ArgumentException("PDF trenutno prima 8-bitne slike."); }
        return all;
    }

    // PDF koristi Flate-kompresovan DeviceGray/DeviceRGB raster. Nema spoljne
    // biblioteke niti privremenih JPEG fajlova, pa isti izlaz radi u paketu.
    private static void SavePdf(IReadOnlyList<ScanImage> pages, string path)
    {
        var objects = new List<byte[]> { Array.Empty<byte>() };
        var pageNumbers = new List<int>();
        foreach (var image in pages)
        {
            var compressed = Deflate(image.Pixels);
            var imageNo = objects.Count + 1;
            objects.Add(StreamObject($"/Type /XObject /Subtype /Image /Width {image.Width} /Height {image.Height} /ColorSpace /Device{(image.Channels == 1 ? "Gray" : "RGB")} /BitsPerComponent 8 /Filter /FlateDecode", compressed));
            var content = Encoding.ASCII.GetBytes($"q\n{image.Width} 0 0 {image.Height} 0 0 cm\n/Im0 Do\nQ\n");
            var contentNo = objects.Count + 1;
            objects.Add(StreamObject(string.Empty, content));
            pageNumbers.Add(objects.Count + 1);
            // Pages object is inserted at #2 once all pages are known; every
            // existing object number is zato pomeren za jedan.
            objects.Add(Encoding.ASCII.GetBytes($"<< /Type /Page /Parent 2 0 R /MediaBox [0 0 {image.Width} {image.Height}] /Resources << /XObject << /Im0 {imageNo + 1} 0 R >> >> /Contents {contentNo + 1} 0 R >>"));
        }
        objects[0] = Encoding.ASCII.GetBytes("<< /Type /Catalog /Pages 2 0 R >>");
        objects.Insert(1, Encoding.ASCII.GetBytes($"<< /Type /Pages /Count {pageNumbers.Count} /Kids [{string.Join(" ", pageNumbers.ConvertAll(n => $"{n + 1} 0 R"))}] >>"));
        using var file = File.Create(path);
        WriteAscii(file, "%PDF-1.4\n");

        // Binarni marker se piše KAO BAJTOVI, ne kroz ASCII enkoder.
        //
        // Encoding.ASCII svaki znak iznad 0x7F pretvara u '?'. Marker je zato
        // izlazio kao "%????" — četiri upitnika umesto bajtova koji alatima
        // kažu da je fajl binaran. Izmereno, ne pretpostavljeno.
        file.Write([(byte)'%', 0xE2, 0xE3, 0xCF, 0xD3, (byte)'\n']);
        var offsets = new List<long> { 0 };
        for (var index = 0; index < objects.Count; index++)
        {
            offsets.Add(file.Position);
            WriteAscii(file, $"{index + 1} 0 obj\n"); file.Write(objects[index]); WriteAscii(file, "\nendobj\n");
        }
        var xref = file.Position;
        WriteAscii(file, $"xref\n0 {objects.Count + 1}\n0000000000 65535 f \n");
        foreach (var offset in offsets.GetRange(1, offsets.Count - 1)) WriteAscii(file, $"{offset:D10} 00000 n \n");
        WriteAscii(file, $"trailer\n<< /Size {objects.Count + 1} /Root 1 0 R >>\nstartxref\n{xref}\n%%EOF\n");
    }

    // ZLibStream, a NE DeflateStream.
    //
    // PDF `/FlateDecode` je zlib (RFC 1950): dva bajta zaglavlja i Adler-32 na
    // kraju. DeflateStream daje goli deflate (RFC 1951), bez oba. Blagi čitači
    // progledaju kroz prste, stroži odbiju sliku — pa se otkaz vidi tek na
    // tuđem računaru, sa drugim čitačem.
    //
    // Izmereno: tok je počinjao bajtom 0x73 umesto 0x78.
    private static byte[] Deflate(byte[] bytes)
    {
        using var target = new MemoryStream();
        using (var stream = new ZLibStream(target, CompressionLevel.Optimal, true))
        {
            stream.Write(bytes);
        }
        return target.ToArray();
    }
    private static byte[] StreamObject(string dictionary, byte[] content) => Encoding.ASCII.GetBytes($"<< {dictionary} /Length {content.Length} >>\nstream\n").Concat(content).Concat(Encoding.ASCII.GetBytes("\nendstream")).ToArray();
    private static void WriteAscii(Stream stream, string value) => stream.Write(Encoding.ASCII.GetBytes(value));
}
