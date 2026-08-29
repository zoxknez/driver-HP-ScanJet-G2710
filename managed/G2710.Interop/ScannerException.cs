using System;

namespace G2710.Interop;

/// <summary>Ishod jedne operacije, isti brojevi kao u <c>g2710_abi.h</c>.</summary>
public enum ScanStatus
{
    Ok = 0,
    NotOpen = 1,
    Timeout = 2,
    ShortTransfer = 3,
    Stalled = 4,
    Cancelled = 5,

    /// <summary>
    /// Veza je nestala USRED operacije. Pozicija glave je od tog trenutka
    /// nepoznata i HOME je obavezan pre bilo cega drugog.
    /// </summary>
    TransportLost = 6,

    DeviceNotFound = 7,
    DeviceError = 8,

    /// <summary>Uredjaj drzi drugi klijent. Vidi <c>Scanner.CurrentOwner</c>.</summary>
    Busy = 9,

    /// <summary>Iznad efektivnog nivoa bezbednosti. Ne popravlja se ponavljanjem.</summary>
    SafetyViolation = 10,

    NotImplemented = 11,
    InvalidArgument = 12,
    InvalidState = 13,
    Internal = 14,
}

/// <summary>
/// Greska sa native strane, sa porukom koju je dao sam uredjaj.
/// </summary>
/// <remarks>
/// Otkazivanje NIJE izuzetak. <see cref="ScanStatus.Cancelled"/> se vraca kao
/// vrednost, jer korisnik koji je pritisnuo "Prekini" nije naisao na gresku -
/// dobio je ono sto je trazio. Izuzetak za otkazivanje bi terao svaki poziv u
/// try/catch i sakrio pravu gresku medju ocekivanim.
/// </remarks>
public sealed class ScannerException : Exception
{
    public ScannerException(ScanStatus status, string detail, uint win32 = 0)
        : base(BuildMessage(status, detail, win32))
    {
        Status = status;
        Detail = detail;
        Win32 = win32;
    }

    public ScanStatus Status { get; }

    /// <summary>Sta je native strana rekla. Prazno ako nije rekla nista.</summary>
    public string Detail { get; }

    /// <summary>Win32 kod ako greska potice iz Win32 poziva; inace 0.</summary>
    public uint Win32 { get; }

    // Tehnicka poruka, na engleskom, za dnevnik i za izuzetak.
    //
    // Tekst koji CITA COVEK ne stoji ovde nego u G2710.Localization: Interop je
    // tehnicki sloj i ne treba da zna na kom jeziku program govori. Ranije je
    // ovde stajao srpski `Describe`, koji nije koristio niko osim sopstvenog
    // testa - dakle prevod koji nikada nije stigao do ekrana.
    private static string BuildMessage(ScanStatus status, string detail, uint win32)
    {
        var text = status.ToString();
        if (!string.IsNullOrWhiteSpace(detail))
        {
            text += ": " + detail;
        }
        if (win32 != 0)
        {
            text += $" [win32 {win32}]";
        }
        return text;
    }

}

/// <summary>
/// Native biblioteka ne odgovara ugovoru po kome je ova strana izgradjena.
/// </summary>
/// <remarks>
/// Poseban tip zato sto je i lek poseban: nijedan drugi poziv nece proci, i
/// nema smisla pokusavati. Jedino sto pomaze je uskladjen par fajlova.
/// </remarks>
public sealed class AbiMismatchException : Exception
{
    public AbiMismatchException(uint expected, uint actual)
        : base($"G2710.Native prijavljuje ABI {Format(actual)}, a ovaj program je " +
               $"gradjen za {Format(expected)}. Fajlovi nisu iz istog izdanja.")
    {
        Expected = expected;
        Actual = actual;
    }

    public uint Expected { get; }
    public uint Actual { get; }

    private static string Format(uint version) => $"{version >> 16}.{version & 0xFFFF}";
}
