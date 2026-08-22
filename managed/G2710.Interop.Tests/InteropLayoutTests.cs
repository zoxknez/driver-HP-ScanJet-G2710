using System.Runtime.InteropServices;
using G2710.Interop;
using Xunit;

namespace G2710.Interop.Tests;

/// <summary>
/// Raspored struktura na .NET strani, i brojevi u enum-ima.
/// </summary>
/// <remarks>
/// Ovo je blizanac tests/unit/abi_stability_test.cpp, i to je poenta: dva
/// NEZAVISNA zapisa istih brojeva. Da je jedan izveden iz drugog, poredjenje bi
/// bilo tautologija.
///
/// Sta se hvata: polje dodato na C++ strani a ne i ovde. Marshalling tada cita
/// pomerene bajtove i tiho vraca smece - broj redova koji ne postoji, duzinu
/// reda koja nije duzina reda. Nista se ne rusi; slika samo bude pogresna.
/// </remarks>
public class InteropLayoutTests
{
    // --- velicine i offseti ---------------------------------------------

    [Fact]
    public void OpenOptionsMatchesTheNativeLayout()
    {
        Assert.Equal(32, Marshal.SizeOf<NativeMethods.OpenOptions>());

        Assert.Equal(0, (int)Marshal.OffsetOf<NativeMethods.OpenOptions>("Size"));
        Assert.Equal(4, (int)Marshal.OffsetOf<NativeMethods.OpenOptions>("Transport"));
        Assert.Equal(8, (int)Marshal.OffsetOf<NativeMethods.OpenOptions>("RequestedSafetyLevel"));

        // Pokazivac je poravnat na 8, pa izmedju njega i prethodnog int-a stoje
        // cetiri bajta popune. Bas u tu popunu bi seo novi int - i nijedan
        // offset se ne bi promenio. Zato C++ strana pamti i SPISAK polja.
        Assert.Equal(16, (int)Marshal.OffsetOf<NativeMethods.OpenOptions>("ClientName"));
        Assert.Equal(24, (int)Marshal.OffsetOf<NativeMethods.OpenOptions>("AcquireTimeoutMs"));
        Assert.Equal(28, (int)Marshal.OffsetOf<NativeMethods.OpenOptions>("RecordTrace"));
    }

    [Fact]
    public void ScanRequestMatchesTheNativeLayout()
    {
        Assert.Equal(48, Marshal.SizeOf<NativeMethods.ScanRequest>());

        Assert.Equal(0, (int)Marshal.OffsetOf<NativeMethods.ScanRequest>("Size"));
        Assert.Equal(4, (int)Marshal.OffsetOf<NativeMethods.ScanRequest>("Resolution"));
        Assert.Equal(8, (int)Marshal.OffsetOf<NativeMethods.ScanRequest>("ColorMode"));
        Assert.Equal(12, (int)Marshal.OffsetOf<NativeMethods.ScanRequest>("BitsPerChannel"));
        Assert.Equal(16, (int)Marshal.OffsetOf<NativeMethods.ScanRequest>("Left"));
        Assert.Equal(20, (int)Marshal.OffsetOf<NativeMethods.ScanRequest>("Top"));
        Assert.Equal(24, (int)Marshal.OffsetOf<NativeMethods.ScanRequest>("Width"));
        Assert.Equal(28, (int)Marshal.OffsetOf<NativeMethods.ScanRequest>("Height"));

        // double trazi poravnanje na 8; ovde je i najlakse promasiti, jer se
        // popuna pre njega ne vidi u izvoru.
        Assert.Equal(32, (int)Marshal.OffsetOf<NativeMethods.ScanRequest>("Gamma"));
        Assert.Equal(40, (int)Marshal.OffsetOf<NativeMethods.ScanRequest>("AllowUnqualified"));
    }

    [Fact]
    public void ScanInfoMatchesTheNativeLayout()
    {
        Assert.Equal(32, Marshal.SizeOf<NativeMethods.ScanInfo>());

        Assert.Equal(0, (int)Marshal.OffsetOf<NativeMethods.ScanInfo>("Size"));
        Assert.Equal(4, (int)Marshal.OffsetOf<NativeMethods.ScanInfo>("WidthPixels"));
        Assert.Equal(8, (int)Marshal.OffsetOf<NativeMethods.ScanInfo>("Lines"));
        Assert.Equal(12, (int)Marshal.OffsetOf<NativeMethods.ScanInfo>("BitsPerChannel"));
        Assert.Equal(16, (int)Marshal.OffsetOf<NativeMethods.ScanInfo>("Channels"));
        Assert.Equal(20, (int)Marshal.OffsetOf<NativeMethods.ScanInfo>("BytesPerLine"));
        Assert.Equal(24, (int)Marshal.OffsetOf<NativeMethods.ScanInfo>("NativeResolution"));
        Assert.Equal(28, (int)Marshal.OffsetOf<NativeMethods.ScanInfo>("ShadingApplied"));
    }

    // --- brojevi u enum-ima ----------------------------------------------

    [Fact]
    public void StatusNumbersMatchTheContract()
    {
        Assert.Equal(0, (int)NativeMethods.Status.Ok);
        Assert.Equal(5, (int)NativeMethods.Status.Cancelled);
        Assert.Equal(6, (int)NativeMethods.Status.TransportLost);
        Assert.Equal(9, (int)NativeMethods.Status.Busy);
        Assert.Equal(10, (int)NativeMethods.Status.SafetyViolation);
        Assert.Equal(14, (int)NativeMethods.Status.Internal);
    }

    [Fact]
    public void ThePublicStatusEnumMirrorsTheInternalOne()
    {
        // Javni ScanStatus je ono sto aplikacija vidi. Da se razidje sa
        // unutrasnjim, prevod bi bio tih: "otkazano" bi postalo "zauzeto".
        foreach (NativeMethods.Status status in Enum.GetValues<NativeMethods.Status>())
        {
            var mapped = (ScanStatus)status;
            Assert.True(Enum.IsDefined(mapped), $"{status} nema par u ScanStatus");
            Assert.Equal(status.ToString(), mapped.ToString());
        }
    }

    [Fact]
    public void ThePublicStateEnumMirrorsTheInternalOne()
    {
        foreach (NativeMethods.DeviceState state in Enum.GetValues<NativeMethods.DeviceState>())
        {
            var mapped = (ScannerState)state;
            Assert.True(Enum.IsDefined(mapped), $"{state} nema par u ScannerState");
            Assert.Equal(state.ToString(), mapped.ToString());
        }
    }

    [Fact]
    public void ThePublicColorModeMirrorsTheInternalOne()
    {
        foreach (NativeMethods.ColorMode mode in Enum.GetValues<NativeMethods.ColorMode>())
        {
            var mapped = (ScanColorMode)mode;
            Assert.True(Enum.IsDefined(mapped), $"{mode} nema par u ScanColorMode");
            Assert.Equal(mode.ToString(), mapped.ToString());
        }
    }

    [Fact]
    public void EveryStatusHasSerbianText()
    {
        // Poruka koju vidi covek. Nedostajuci prevod bi ispao kao "Nepoznat
        // ishod" bas u trenutku kada je objasnjenje najpotrebnije.
        foreach (ScanStatus status in Enum.GetValues<ScanStatus>())
        {
            Assert.NotEqual("Nepoznat ishod", ScannerException.Describe(status));
        }
    }
}
