using System;
using System.Globalization;
using G2710.Localization;

namespace G2710.App.Tests;

/// <summary>
/// Privremeno prebaci program na odredjeni jezik i vrati ga kako je bio.
/// </summary>
/// <remarks>
/// Jezik je proces-siroko stanje, pa test koji ga menja mora da ga i vrati -
/// inace bi sledeci test citao natpise na jeziku koji nije trazio, i pao bi
/// ili prosao zavisno od redosleda.
/// </remarks>
internal sealed class LanguageScope : IDisposable
{
    private readonly CultureInfo? previous;

    public LanguageScope(string language)
    {
        previous = Language.Override;
        Language.Override = CultureInfo.GetCultureInfo(language);
    }

    public void Dispose() => Language.Override = previous;
}
