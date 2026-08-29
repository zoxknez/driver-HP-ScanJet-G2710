using System;
using System.Windows.Markup;

namespace G2710.Localization;

/// <summary>
/// XAML pristup tekstu: <c>Text="{loc:Str App_Title}"</c>.
/// </summary>
/// <remarks>
/// Postoji da XAML ostane citljiv. Alternativa je bila da svaka niska ide kroz
/// binding na property u view model-u, pa bi svaki natpis - i onaj koji se
/// nikada ne menja - dobio svoje polje. Prozor bi tada imao sto polja koja
/// nista ne rade osim sto vracaju konstantu.
///
/// Vrednost se razresava JEDNOM, pri ucitavanju prozora. Jezik se bira pri
/// instalaciji i ne menja se dok program radi, pa dinamicko osvezavanje ne bi
/// imalo sta da osvezi.
/// </remarks>
[MarkupExtensionReturnType(typeof(string))]
public sealed class StrExtension : MarkupExtension
{
    public StrExtension()
    {
    }

    public StrExtension(string key)
    {
        Key = key;
    }

    [ConstructorArgument("key")]
    public string Key { get; set; } = string.Empty;

    public override object ProvideValue(IServiceProvider serviceProvider) =>
        string.IsNullOrWhiteSpace(Key) ? string.Empty : Strings.Get(Key);
}
