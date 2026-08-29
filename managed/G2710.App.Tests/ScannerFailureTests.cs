using System;
using System.Globalization;
using System.Linq;
using G2710.App;
using G2710.Interop;
using G2710.Localization;
using Xunit;

namespace G2710.App.Tests;

/// <summary>
/// Tekst koji korisnik cita kada nesto ne uspe.
/// </summary>
/// <remarks>
/// Ovo je nastalo iz stvarnog propusta. Aplikacija je imala poseban slucaj za
/// tri statusa, a svih ostalih dvanaest je padalo u `default` i prikazivalo
/// `exception.Message` - tehnicku nisku napravljenu za dnevnik. Prevodi za
/// svih petnaest statusa postojali su u resursima od pocetka; trinaest ih niko
/// nije trazio.
///
/// Kvar je bio tih: program radi, poruka postoji, samo je pogresna vrsta
/// poruke. Ovi testovi ga drze zatvorenim, i to na SVAKOM statusu - da se novi
/// status ne moze dodati bez prevoda.
/// </remarks>
public sealed class ScannerFailureTests
{
    [Theory]
    [InlineData("en")]
    [InlineData("sr")]
    public void EveryScannerStatusHasHumanText(string language)
    {
        using var _ = new LanguageScope(language);

        foreach (ScanStatus status in Enum.GetValues<ScanStatus>())
        {
            string title = MainViewModel.TitleFor(status);

            Assert.False(string.IsNullOrWhiteSpace(title), $"{status} nema tekst");

            // Strings.Get vraca "[Kljuc]" za nepoznat kljuc. Da je to prislo do
            // ekrana, korisnik bi video uglaste zagrade umesto recenice.
            Assert.False(title.StartsWith('['), $"{status} nema prevod: {title}");

            // Ime iz enum-a NIJE tekst za coveka. Ovo hvata i slucaj kada bi
            // neko "preveo" kljuc tako sto u njega upise ime statusa.
            Assert.NotEqual(status.ToString(), title);
        }
    }

    [Fact]
    public void The_two_languages_really_say_different_things()
    {
        // Bez ovoga bi test iznad prolazio i kada srpski satelit uopste ne bi
        // bio ucitan - sve bi bilo na engleskom, i sve bi "imalo tekst".
        string english, serbian;
        using (var _ = new LanguageScope("en")) { english = MainViewModel.TitleFor(ScanStatus.Timeout); }
        using (var _ = new LanguageScope("sr")) { serbian = MainViewModel.TitleFor(ScanStatus.Timeout); }

        Assert.NotEqual(english, serbian);
    }

    [Fact]
    public void A_timeout_no_longer_shows_the_log_line_as_the_title()
    {
        // Tacno onaj kvar zbog koga ovaj fajl postoji.
        using var _ = new LanguageScope("sr");

        var model = new MainViewModel();
        model.SetScannerFailure(new ScannerException(ScanStatus.Timeout, "bulk read", 1460));

        Assert.DoesNotContain("Timeout", model.StatusTitle, StringComparison.Ordinal);
        Assert.DoesNotContain("win32", model.StatusTitle, StringComparison.Ordinal);

        // Tehnicki detalj se ne gubi - samo se ne predstavlja kao objasnjenje.
        Assert.Contains("bulk read", model.StatusDetail, StringComparison.Ordinal);
    }

    [Fact]
    public void A_busy_scanner_names_who_is_holding_it()
    {
        using var _ = new LanguageScope("en");

        var model = new MainViewModel();
        model.SetScannerFailure(new ScannerException(ScanStatus.Busy, "arbiter", 0), "Windows Fax and Scan");

        Assert.Contains("Windows Fax and Scan", model.StatusDetail, StringComparison.Ordinal);
    }

    [Fact]
    public void An_unknown_status_falls_back_instead_of_showing_a_number()
    {
        using var _ = new LanguageScope("en");

        // Vrednost koja ne postoji u enum-u moze stici sa native strane ako se
        // dve strane raziđu. Korisnik tada ne sme videti broj.
        string title = MainViewModel.TitleFor((ScanStatus)99);

        Assert.False(title.StartsWith('['));
        Assert.NotEqual("99", title);
    }
}
