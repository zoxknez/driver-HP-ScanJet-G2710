using System;
using System.IO;
using G2710.App;
using G2710.Interop;
using Xunit;

namespace G2710.App.Tests;

/// <summary>
/// Sta korisnik vidi kada pored programa stoji pogresna biblioteka.
/// </summary>
/// <remarks>
/// Nastalo iz dva kvara koja su stajala jedan pored drugog.
///
/// Prvi: provera ABI verzije zvala se tek na "Proveri vezu", a dugme
/// "Skeniraj" je aktivno od prve sekunde - <c>CanScan</c> gleda samo da li
/// skeniranje vec traje. Prvi poziv u biblioteku je mogao proci NEPROVEREN,
/// bas ono od cega provera cuva. Njen sopstveni komentar je tvrdio da se zove
/// pri pokretanju; nije se.
///
/// Drugi: <see cref="AbiMismatchException"/> nije <see cref="ScannerException"/>,
/// pa je padala u opsti catch i prikazivala se kao "skener nije spreman".
/// Korisnik bi proveravao kabl, a fajlovi pored programa su bili iz razlicitih
/// izdanja.
/// </remarks>
public sealed class LibraryCheckTests
{
    [Fact]
    public void A_matching_library_says_nothing()
    {
        Assert.Null(LibraryCheck.Check(() => Scanner.ExpectedAbiVersion));
    }

    [Theory]
    [InlineData("en")]
    [InlineData("sr")]
    public void A_mismatch_names_both_versions(string language)
    {
        using var _ = new LanguageScope(language);

        // Starija biblioteka: 1.0 naspram ocekivanog 1.1.
        string? problem = LibraryCheck.Check(() => (1u << 16) | 0u);

        Assert.NotNull(problem);
        Assert.Contains("1.1", problem, StringComparison.Ordinal);
        Assert.Contains("1.0", problem, StringComparison.Ordinal);
    }

    [Fact]
    public void A_mismatch_does_not_talk_about_the_scanner()
    {
        // Ovo je onaj kvar: poruka je govorila da skener nije spreman, a skener
        // sa tim nema nikakve veze.
        using var _ = new LanguageScope("en");

        string? problem = LibraryCheck.Check(() => 0u);

        Assert.NotNull(problem);
        Assert.DoesNotContain("not ready", problem, StringComparison.OrdinalIgnoreCase);
        Assert.Contains("install", problem, StringComparison.OrdinalIgnoreCase);
    }

    [Theory]
    [InlineData("en")]
    [InlineData("sr")]
    public void A_missing_library_is_reported_as_an_incomplete_installation(string language)
    {
        using var _ = new LanguageScope(language);

        string? problem = LibraryCheck.Check(
            () => throw new DllNotFoundException("G2710.Native.dll"));

        Assert.NotNull(problem);
        Assert.Contains("G2710.Native.dll", problem, StringComparison.Ordinal);
    }

    [Fact]
    public void A_wrong_architecture_is_reported_too()
    {
        // x86 biblioteka pored x64 programa. Za korisnika je to ista prica:
        // instalacija nije potpuna.
        using var _ = new LanguageScope("en");

        string? problem = LibraryCheck.Check(
            () => throw new BadImageFormatException("wrong architecture"));

        Assert.NotNull(problem);
    }

    [Fact]
    public void An_unexpected_failure_is_not_swallowed()
    {
        // Samo poznati oblici otkaza postaju poruka. Sve ostalo mora izaci -
        // provera koja proguta nepoznat izuzetak sakriva pravi razlog.
        Assert.Throws<InvalidOperationException>(
            () => LibraryCheck.Check(() => throw new InvalidOperationException("nesto drugo")));
    }
}
