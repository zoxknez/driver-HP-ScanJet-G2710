using System;
using Microsoft.Win32.SafeHandles;

namespace G2710.Interop;

/// <summary>
/// Otvoren uredjaj. Zatvara se sam, i onda kada se aplikacija srusi.
/// </summary>
/// <remarks>
/// SafeHandle, ne IntPtr, i to nije stvar stila.
///
/// Uredjaj koji ostane otvoren drzi ekskluzivnu bravu u <c>Global\</c>
/// namespace-u. Ako aplikacija padne pre <c>g2710_close</c>, ta brava ostaje
/// zauzeta i sledeci klijent - WIA servis, TWAIN, drugi pokusaj iste
/// aplikacije - zatice skener koji "koristi neko drugi", bez ijednog vidljivog
/// procesa koji bi ga koristio.
///
/// SafeHandle to resava jer ga runtime zatvara u critical finalizer-u: izvrsava
/// se i kada obican finalizer ne stigne. To je jedina odbrana koju .NET nudi
/// protiv zaboravljenog uredjaja.
///
/// Nasledjuje ZeroOrMinusOneIsInvalid jer <c>g2710_open</c> pri gresci ostavlja
/// NULL - to je zapisano u ugovoru i ovde se na to oslanjamo.
/// </remarks>
public sealed class ScannerHandle : SafeHandleZeroOrMinusOneIsInvalid
{
    /// <summary>Runtime ga poziva pri marshalling-u izlaznog parametra.</summary>
    public ScannerHandle() : base(ownsHandle: true)
    {
    }

    protected override bool ReleaseHandle()
    {
        // g2710_close prima sirov pokazivac, ne SafeHandle: SafeHandle koji
        // zatvara sam sebe je rekurzija koju runtime odbija.
        NativeMethods.Close(handle);
        return true;
    }
}
