using System;
using System.Globalization;
using System.Windows;
using System.Windows.Data;

namespace G2710.App;

/// <summary>
/// Prikazuje element samo dok vezana vrednost NE postoji.
/// </summary>
/// <remarks>
/// Sluzi uputstvu u praznom prikazu: cim stigne slika, uputstvo nestaje.
///
/// Namerno se vezuje za samu sliku, a ne za zaseban logicki znak - dva izvora
/// istine o istoj stvari se pre ili kasnije raziđu, i tada uputstvo stoji preko
/// slike ili prazan okvir ostane nem.
/// </remarks>
public sealed class NullToVisibleConverter : IValueConverter
{
    public object Convert(object? value, Type targetType, object? parameter, CultureInfo culture) =>
        value is null ? Visibility.Visible : Visibility.Collapsed;

    public object ConvertBack(object? value, Type targetType, object? parameter, CultureInfo culture) =>
        throw new NotSupportedException("Uputstvo se ne uredjuje; ide samo u jednom smeru.");
}
