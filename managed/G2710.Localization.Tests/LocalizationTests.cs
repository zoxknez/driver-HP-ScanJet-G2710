using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Xml.Linq;
using G2710.Localization;
using Xunit;

namespace G2710.Localization.Tests;

/// <summary>
/// Prevodi, i redosled po kome se jezik bira.
/// </summary>
/// <remarks>
/// Dva fajla resursa citaju se kao XML, a ne kroz ResourceManager. Razlog je
/// isti kao kod ABI stabilnosti: poredjenje koje ide kroz isti mehanizam koji
/// meri ne bi otkrilo kljuc koji nedostaje - vratilo bi fallback i cutalo.
/// </remarks>
public sealed class LocalizationTests
{
    private static readonly string Root =
        Path.Combine(AppContext.BaseDirectory, "..", "..", "..", "..",
                     "G2710.Localization", "Resources");

    private static HashSet<string> KeysIn(string file)
    {
        string path = Path.GetFullPath(Path.Combine(Root, file));
        Assert.True(File.Exists(path), $"nema {path}");

        return XDocument.Load(path).Root!
            .Elements("data")
            .Select(e => e.Attribute("name")!.Value)
            .ToHashSet(StringComparer.Ordinal);
    }

    private static Dictionary<string, string> ValuesIn(string file)
    {
        string path = Path.GetFullPath(Path.Combine(Root, file));
        return XDocument.Load(path).Root!
            .Elements("data")
            .ToDictionary(e => e.Attribute("name")!.Value,
                          e => e.Element("value")?.Value ?? string.Empty,
                          StringComparer.Ordinal);
    }

    // =========================================================================
    // Dva fajla moraju nositi ISTE kljuceve
    // =========================================================================

    [Fact]
    public void Every_english_key_has_a_serbian_translation()
    {
        var english = KeysIn("Strings.resx");
        var serbian = KeysIn("Strings.sr.resx");

        // Kljuc koji postoji samo na engleskom je engleski natpis usred srpskog
        // prozora - i to se ne vidi dok neko ne pokrene program na srpskom.
        var missing = english.Except(serbian).Order().ToList();
        Assert.True(missing.Count == 0, "bez srpskog prevoda: " + string.Join(", ", missing));
    }

    [Fact]
    public void Every_serbian_key_exists_in_english()
    {
        var english = KeysIn("Strings.resx");
        var serbian = KeysIn("Strings.sr.resx");

        // Kljuc koji postoji samo na srpskom je prevod koji program nikada ne
        // trazi - mrtav tekst koji izgleda kao da nesto radi.
        var orphans = serbian.Except(english).Order().ToList();
        Assert.True(orphans.Count == 0, "nema engleski original: " + string.Join(", ", orphans));
    }

    [Fact]
    public void No_translation_is_empty()
    {
        foreach (var (key, value) in ValuesIn("Strings.sr.resx"))
        {
            Assert.False(string.IsNullOrWhiteSpace(value), $"prazan prevod: {key}");
        }
        foreach (var (key, value) in ValuesIn("Strings.resx"))
        {
            Assert.False(string.IsNullOrWhiteSpace(value), $"prazan original: {key}");
        }
    }

    [Fact]
    public void Placeholders_match_between_the_two_languages()
    {
        // Niska sa {0} na jednom jeziku a bez njega na drugom pada tek kada se
        // formatira - dakle u radu, kod korisnika, i samo na jednom jeziku.
        var english = ValuesIn("Strings.resx");
        var serbian = ValuesIn("Strings.sr.resx");

        foreach (var (key, value) in english)
        {
            if (!serbian.TryGetValue(key, out string? translated))
            {
                continue;   // pokriva drugi test
            }
            for (int index = 0; index < 3; ++index)
            {
                string placeholder = "{" + index + "}";
                Assert.Equal(value.Contains(placeholder, StringComparison.Ordinal),
                             translated.Contains(placeholder, StringComparison.Ordinal));
            }
        }
    }

    // =========================================================================
    // Redosled odluke o jeziku
    // =========================================================================

    [Fact]
    public void English_is_what_you_get_when_nothing_says_otherwise()
    {
        // Engleski je i podrazumevan i poslednja odbrana.
        CultureInfo chosen = Language.Decide(null, null, null, new CultureInfo("de-DE"));
        Assert.Equal(CultureInfo.InvariantCulture, chosen);
    }

    [Fact]
    public void The_choice_made_at_install_time_beats_the_system_language()
    {
        // Korisnik koji je pri instalaciji izabrao srpski dobija srpski i na
        // engleskom Windows-u.
        CultureInfo chosen = Language.Decide(null, "sr", null, new CultureInfo("en-US"));
        Assert.Equal("sr", chosen.TwoLetterISOLanguageName);
    }

    [Fact]
    public void An_explicit_choice_beats_everything()
    {
        CultureInfo chosen = Language.Decide(new CultureInfo("sr-Latn-RS"), "en", "en",
                                             new CultureInfo("en-US"));
        Assert.Equal("sr", chosen.TwoLetterISOLanguageName);
    }

    [Fact]
    public void A_serbian_windows_gets_serbian_without_any_setting()
    {
        // Program prekopiran bez instalacije nema zapis u registru; jezik
        // sistema je tada jedini nagovestaj koji postoji.
        CultureInfo chosen = Language.Decide(null, null, null, new CultureInfo("sr-Latn-RS"));
        Assert.Equal("sr", chosen.TwoLetterISOLanguageName);
    }

    [Fact]
    public void Choosing_english_at_install_time_is_not_the_same_as_saying_nothing()
    {
        // Ovo je pao kada je napisan. Instalater UVEK upisuje vrednost, pa se
        // "en" mora citati kao odluka; dok se odbacivao kao "nije satelit",
        // korisnik koji je izabrao English dobijao bi srpski cim mu je Windows
        // srpski - izbor koji je napravio bio bi tiho preskocen.
        CultureInfo chosen = Language.Decide(null, "en", null, new CultureInfo("sr-Latn-RS"));
        Assert.Equal(CultureInfo.InvariantCulture, chosen);
    }

    [Fact]
    public void The_file_beside_a_portable_package_beats_the_system_language()
    {
        // Kvalifikacioni ZIP se ne instalira, pa registar niko nije upisao.
        // Bez ovog koraka bi paket poslat na srpskom progovorio engleski cim
        // bi ga neko otvorio na engleskom Windows-u.
        CultureInfo chosen = Language.Decide(null, null, "sr", new CultureInfo("en-US"));
        Assert.Equal("sr", chosen.TwoLetterISOLanguageName);
    }

    [Fact]
    public void What_the_installer_wrote_beats_the_file_beside_the_program()
    {
        // Instaliran proizvod je jaci od fajla koji je neko doneo pored njega:
        // izbor napravljen pri instalaciji je izbor OVOG racunara.
        CultureInfo chosen = Language.Decide(null, "en", "sr", new CultureInfo("sr-Latn-RS"));
        Assert.Equal(CultureInfo.InvariantCulture, chosen);
    }

    [Fact]
    public void An_unsupported_language_falls_back_to_english_instead_of_throwing()
    {
        // Program koji se srusi zato sto mu je neko dao nepoznat jezik je gori
        // od programa koji govori engleski.
        Assert.Equal(CultureInfo.InvariantCulture,
                     Language.Decide(new CultureInfo("ja-JP"), null, null, null));
        Assert.Equal(CultureInfo.InvariantCulture,
                     Language.Decide(null, "klingon", null, null));
    }

    // =========================================================================
    // Tekst zaista stize na oba jezika
    // =========================================================================

    [Fact]
    public void The_same_key_gives_different_text_in_the_two_languages()
    {
        try
        {
            Language.Override = CultureInfo.InvariantCulture;
            string english = Strings.Get("App_Button_Scan");

            Language.Override = new CultureInfo("sr");
            string serbian = Strings.Get("App_Button_Scan");

            Assert.Equal("Scan", english);
            Assert.Equal("Skeniraj", serbian);
        }
        finally
        {
            Language.Override = null;
        }
    }

    [Fact]
    public void A_missing_key_is_visible_but_does_not_crash()
    {
        // Prazan ili srusen prozor zbog jedne niske je gori od jedne cudne reci
        // na ekranu.
        Assert.Equal("[Nema_Ovakvog_Kljuca]", Strings.Get("Nema_Ovakvog_Kljuca"));
    }

    [Fact]
    public void Format_puts_the_values_where_the_translation_wants_them()
    {
        try
        {
            Language.Override = CultureInfo.InvariantCulture;
            Assert.Equal("600 dpi", Strings.Format("Res_Label", 600));
        }
        finally
        {
            Language.Override = null;
        }
    }
}
