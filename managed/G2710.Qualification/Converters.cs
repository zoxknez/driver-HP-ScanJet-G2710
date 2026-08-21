using System;
using System.Globalization;
using System.Windows;
using System.Windows.Data;
using System.Windows.Media;

namespace G2710.Qualification;

/// <summary>true -> Visible, false -> Collapsed.</summary>
public sealed class BoolToVisibleConverter : IValueConverter
{
    public object Convert(object? value, Type targetType, object? parameter, CultureInfo culture)
        => value is true ? Visibility.Visible : Visibility.Collapsed;

    public object ConvertBack(object? value, Type targetType, object? parameter,
                              CultureInfo culture)
        => value is Visibility.Visible;
}

/// <summary>Obrnuta logicka vrednost.</summary>
public sealed class NotConverter : IValueConverter
{
    public object Convert(object? value, Type targetType, object? parameter, CultureInfo culture)
        => value is not true;

    public object ConvertBack(object? value, Type targetType, object? parameter,
                              CultureInfo culture)
        => value is not true;
}

/// <summary>
/// Naziv tona iz modela prikaza u cetkicu iz palete.
/// </summary>
/// <remarks>
/// Model prikaza NE zna za boje - vraca "Pass", "Fail", "Wait", "Ask". Da vraca
/// cetkicu, paleta bi se rasula po ViewModel-ima i tema se vise ne bi mogla
/// promeniti na jednom mestu.
/// </remarks>
public sealed class ToneToBrushConverter : IValueConverter
{
    /// <summary>Kada je true, uzima blagu varijantu (za pozadinu znacke).</summary>
    public bool Soft { get; set; }

    public object Convert(object? value, Type targetType, object? parameter, CultureInfo culture)
    {
        var tone = value as string ?? "Wait";
        var key = Soft ? tone + "Soft" : tone;

        if (Application.Current?.TryFindResource(key) is Brush brush)
        {
            return brush;
        }
        return Soft ? Brushes.Transparent : Brushes.Black;
    }

    public object ConvertBack(object? value, Type targetType, object? parameter,
                              CultureInfo culture)
        => throw new NotSupportedException("Boja se ne pretvara nazad u ton.");
}
