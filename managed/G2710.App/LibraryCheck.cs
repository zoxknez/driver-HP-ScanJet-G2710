using System;
using System.IO;
using G2710.Interop;
using G2710.Localization;

namespace G2710.App;

/// <summary>
/// Provera koja mora proci PRE nego sto program dodirne native biblioteku.
/// </summary>
/// <remarks>
/// Klasa se NE zove Startup: to ime u <see cref="System.Windows.Application"/>
/// vec pripada dogadjaju, pa bi poziv iz App.xaml.cs pogadjao njega i program
/// se ne bi ni preveo. Ime koje se sudara sa nasledjenim clanom je zamka koja
/// ceka sledeceg.
/// </remarks>
/// <remarks>
/// Provera ABI verzije je postojala i ranije, ali se zvala tek kada korisnik
/// klikne "Proveri vezu". Dugme "Skeniraj" je bilo aktivno od prve sekunde -
/// <c>CanScan</c> gleda samo da li skeniranje vec traje - pa je prvi poziv u
/// biblioteku mogao proci NEPROVEREN. Bas ono od cega provera cuva.
///
/// I poruka je bila pogresna: <see cref="AbiMismatchException"/> nije
/// <see cref="ScannerException"/>, pa je padala u opsti catch i prikazivala se
/// kao "skener nije spreman". Korisnik bi proveravao kabl, a problem je bio u
/// tome sto pored programa stoji biblioteka iz drugog izdanja.
/// </remarks>
public static class LibraryCheck
{
    /// <summary>
    /// Ispituje da li se program i biblioteka slazu.
    /// </summary>
    /// <param name="readNativeVersion">
    /// Cita verziju iz biblioteke. Izdvojeno kao parametar da se sva tri
    /// ishoda mogu izmeriti bez zamene fajlova na disku.
    /// </param>
    /// <returns>
    /// <c>null</c> ako je sve u redu; inace poruka za korisnika, na jeziku
    /// kojim program govori.
    /// </returns>
    public static string? Check(Func<uint>? readNativeVersion = null)
    {
        Func<uint> read = readNativeVersion ?? (() => Scanner.NativeAbiVersion);

        uint actual;
        try
        {
            actual = read();
        }
        catch (Exception exception) when (exception is DllNotFoundException
                                              or BadImageFormatException
                                              or EntryPointNotFoundException
                                              or FileNotFoundException)
        {
            // Biblioteke nema, pogresna je arhitektura, ili nema trazenu
            // funkciju. Za korisnika je to jedna te ista stvar: instalacija
            // nije potpuna. Tehnicki razlog ide u drugi red.
            return Strings.Get("App_Fatal_NoLibrary") + "\n\n" + exception.Message;
        }

        if (actual == Scanner.ExpectedAbiVersion)
        {
            return null;
        }
        return Strings.Format("App_Fatal_AbiMismatch",
                              Format(Scanner.ExpectedAbiVersion), Format(actual));
    }

    private static string Format(uint version) => $"{version >> 16}.{version & 0xFFFF}";
}
