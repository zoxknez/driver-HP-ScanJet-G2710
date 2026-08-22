using System;
using G2710.Interop;

namespace G2710.App;

/// <summary>
/// Preslikava izbor na preview-u u oblast koju skener dobija na ciljnom DPI-ju.
/// Koordinate preview-a su u pikselima; izlaz je u pikselima tražene rezolucije.
/// </summary>
public static class CropTransform
{
    public readonly record struct Rect(int Left, int Top, int Width, int Height)
    {
        public int Right => checked(Left + Width);
        public int Bottom => checked(Top + Height);
        public bool IsEmpty => Width <= 0 || Height <= 0;
    }

    /// <summary>
    /// Izbor sa prikaza pretvara u zahtev za finalni prolaz. Izlaz se uvek
    /// seče na granice finalne slike; nikada se ne šalju negativne koordinate.
    /// </summary>
    public static ScanSettings ToScanSettings(
        Rect previewSelection, ScanGeometry preview, ScanGeometry target,
        ScanSettings requested)
    {
        ArgumentNullException.ThrowIfNull(preview);
        ArgumentNullException.ThrowIfNull(target);
        ArgumentNullException.ThrowIfNull(requested);
        if (preview.WidthPixels <= 0 || preview.Lines <= 0 || target.WidthPixels <= 0 || target.Lines <= 0)
            throw new ArgumentOutOfRangeException(nameof(preview), "Geometrija mora imati pozitivne dimenzije.");

        if (previewSelection.IsEmpty)
            return requested with { Left = 0, Top = 0, Width = 0, Height = 0 };

        // Skalira se krajnja granica, a ne sama širina. Tako susedni crop-ovi
        // ostaju susedni i kada odnos rezolucija nije ceo broj.
        var left = ScaleAndClamp(previewSelection.Left, preview.WidthPixels, target.WidthPixels);
        var top = ScaleAndClamp(previewSelection.Top, preview.Lines, target.Lines);
        var right = ScaleAndClamp(previewSelection.Right, preview.WidthPixels, target.WidthPixels);
        var bottom = ScaleAndClamp(previewSelection.Bottom, preview.Lines, target.Lines);
        if (right <= left || bottom <= top)
            throw new ArgumentException("Izbor nema nijedan piksel nakon presecanja.", nameof(previewSelection));

        return requested with { Left = left, Top = top, Width = right - left, Height = bottom - top };
    }

    private static int ScaleAndClamp(int coordinate, int sourceLength, int targetLength)
    {
        var bounded = Math.Clamp(coordinate, 0, sourceLength);
        return (int)Math.Clamp((long)bounded * targetLength / sourceLength, 0, targetLength);
    }
}
