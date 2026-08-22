using System;
using G2710.Interop;

namespace G2710.App;

/// <summary>Bezbedna granica između redova iz ABI-ja i slike za prikaz/izvoz.</summary>
public sealed class ScanCapture
{
    private readonly ScanGeometry _geometry;
    private readonly byte[] _pixels;
    private int _nextLine;

    public ScanCapture(ScanGeometry geometry)
    {
        ArgumentNullException.ThrowIfNull(geometry);
        if (geometry.WidthPixels <= 0 || geometry.Lines <= 0 || geometry.BytesPerLine <= 0)
            throw new ArgumentException("Nevažeća geometrija skena.", nameof(geometry));
        _geometry = geometry;
        _pixels = new byte[checked(geometry.BytesPerLine * geometry.Lines)];
    }

    public int LinesReceived => _nextLine;
    public int TotalLines => _geometry.Lines;
    public int Percent => Math.Min(100, _nextLine * 100 / _geometry.Lines);
    public Span<byte> NextLine => _nextLine < _geometry.Lines
        ? _pixels.AsSpan(_nextLine * _geometry.BytesPerLine, _geometry.BytesPerLine)
        : throw new InvalidOperationException("Svi redovi su već primljeni.");

    public void CommitLine()
    {
        if (_nextLine >= _geometry.Lines) throw new InvalidOperationException("Previše redova iz skenera.");
        _nextLine++;
    }

    /// <summary>Vraća samo potpunu sliku. Otkazani prolaz nema rezultat za izvoz.</summary>
    public ScanImage Complete()
    {
        if (_nextLine != _geometry.Lines)
            throw new InvalidOperationException("Skeniranje nije kompletno; slika se ne sme sačuvati.");
        return new ScanImage(_geometry.WidthPixels, _geometry.Lines, _geometry.BitsPerChannel,
            _geometry.Channels, _pixels);
    }
}

public static class ScanWorkflow
{
    /// <summary>
    /// Čita ceo prolaz. Cancelled je normalan ishod i vraća null, nikada
    /// delimičnu sliku. ScanEnd ide u finally čak i kada red ili callback padne.
    /// </summary>
    public static ScanImage? Run(Scanner scanner, ScanSettings settings, Action<int>? progress = null)
    {
        ArgumentNullException.ThrowIfNull(scanner);
        ArgumentNullException.ThrowIfNull(settings);
        var geometry = scanner.ScanBegin(settings);
        try
        {
            var capture = new ScanCapture(geometry);
            while (true)
            {
                var result = scanner.ScanReadLine(capture.NextLine);
                if (result == ScanLineResult.Cancelled) return null;
                if (result == ScanLineResult.Complete) return capture.Complete();
                capture.CommitLine();
                progress?.Invoke(capture.Percent);
                // Native protokol kraj često javi TEK pri sledećem čitanju.
                // Pošto plan unapred daje tačan broj redova, nema razloga da
                // tada prosledimo nepostojeći bafer samo radi tog signala.
                if (capture.LinesReceived == capture.TotalLines) return capture.Complete();
            }
        }
        finally
        {
            scanner.ScanEnd();
        }
    }
}
