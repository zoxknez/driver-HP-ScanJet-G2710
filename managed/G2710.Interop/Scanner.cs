using System;
using System.Runtime.InteropServices;
using System.Text;

namespace G2710.Interop;

/// <summary>Odakle dolaze podaci.</summary>
public enum ScannerTransport
{
    /// <summary>Pravi uredjaj.</summary>
    UsbScan = 0,

    /// <summary>
    /// Simulator. Postoji da bi se aplikacija mogla voziti cela, bez skenera.
    /// </summary>
    Simulator = 1,
}

public enum ScanColorMode
{
    Color = 0,
    Gray = 1,
    Lineart = 2,
}

public enum ScannerState
{
    Disconnected = 0,
    Opened = 1,
    Identified = 2,
    Idle = 3,
    WarmingUp = 4,
    Homing = 5,
    Calibrating = 6,
    Scanning = 7,
    Cancelling = 8,
    TransportLost = 9,
    Faulted = 10,
    EmergencyStopped = 11,
}

/// <summary>Sta se trazi od skenera.</summary>
public sealed record ScanSettings
{
    public int Resolution { get; init; } = 300;
    public ScanColorMode ColorMode { get; init; } = ScanColorMode.Color;
    public int BitsPerChannel { get; init; } = 8;

    /// <summary>Oblast u pikselima na TRAZENOJ rezoluciji. Sve nule = cela povrsina.</summary>
    public int Left { get; init; }
    public int Top { get; init; }
    public int Width { get; init; }
    public int Height { get; init; }

    /// <summary>1.0 je bez korekcije.</summary>
    public double Gamma { get; init; } = 1.0;

    /// <summary>
    /// Dozvoli i rezolucije koje hardver nije potvrdio. Dijagnostika da; ono
    /// sto se nudi krajnjem korisniku ne.
    /// </summary>
    public bool AllowUnqualified { get; init; }
}

/// <summary>
/// Ishod jednog citanja reda.
/// </summary>
/// <remarks>
/// Tri vrednosti, ne bool. Prva verzija je vracala <c>bool</c> - "ima jos" -
/// pa je otkazan prolaz izgledao IDENTICNO kao isporucen red: otkazivanje se
/// gutalo kao "nije greska", a `done` je ostajao nula. Aplikacija bi tako
/// upisala nepotpunu sliku i nikome ne bi rekla da je nepotpuna.
///
/// Bool ne moze da nosi tri ishoda. Zato ih i ne nosi.
/// </remarks>
public enum ScanLineResult
{
    /// <summary>Red je popunjen.</summary>
    Delivered,

    /// <summary>Slika je gotova; red NIJE popunjen.</summary>
    Complete,

    /// <summary>Prekinuto; slika je NEPOTPUNA.</summary>
    Cancelled,
}

/// <summary>Geometrija koja se zna tek kada prolaz pocne.</summary>
public sealed record ScanGeometry(
    int WidthPixels,
    int Lines,
    int BitsPerChannel,
    int Channels,
    int BytesPerLine,
    int NativeResolution,
    bool ShadingApplied);

/// <summary>Podesavanja otvaranja.</summary>
public sealed record ScannerOptions
{
    public ScannerTransport Transport { get; init; } = ScannerTransport.UsbScan;

    /// <summary>
    /// 1..5. Efektivni nivo je min(plafon build-a, ovo) - plafon se ovim NE
    /// MOZE podici.
    /// </summary>
    public int RequestedSafetyLevel { get; init; } = 1;

    /// <summary>Ime koje vidi sledeci klijent kada zatekne zauzet uredjaj.</summary>
    public string ClientName { get; init; } = "G2710";

    public TimeSpan AcquireTimeout { get; init; } = TimeSpan.Zero;

    /// <summary>Snimaj svaki transfer. Ukljucuje se SAMO pri otvaranju.</summary>
    public bool RecordTrace { get; init; }
}

/// <summary>
/// Skener, kako ga vidi .NET.
/// </summary>
/// <remarks>
/// Ovo je sloj ODLUKA nad <see cref="NativeMethods"/>, koji je samo doslovan
/// prevod zaglavlja. Pravilo je isto kao u C++ delu: sve sto se moze pogresiti
/// stoji na jednom mestu i testira se.
///
/// NITI: jedan objekat je jedan pozivalac. Jedini izuzetak je
/// <see cref="Cancel"/>, koji SME iz bilo koje niti - to je i njegova svrha,
/// jer dugme "Prekini" ne zivi na niti koja skenira.
/// </remarks>
public sealed class Scanner : IDisposable
{
    private readonly ScannerHandle _handle;
    private Callbacks.Pin _logPin;
    private bool _logPinned;
    private bool _disposed;

    private Scanner(ScannerHandle handle)
    {
        _handle = handle;
    }

    // --- ugovor ---------------------------------------------------------

    /// <summary>Verzija ABI-ja za koju je OVA strana izgradjena.</summary>
    public static uint ExpectedAbiVersion => (1u << 16) | 0u;

    /// <summary>Verzija koju prijavljuje ucitana biblioteka.</summary>
    public static uint NativeAbiVersion => NativeMethods.AbiVersion();

    /// <summary>Plafon ugradjen u binarni fajl. Ne moze se podici.</summary>
    public static int BuildSafetyCeiling => NativeMethods.BuildSafetyCeiling();

    /// <summary>
    /// Da li je motorni kod uopste preveden. False znaci da paket ne moze
    /// pomeriti glavu ni ako se to zatrazi.
    /// </summary>
    public static bool MotorPathCompiled => NativeMethods.MotorPathCompiled() != 0;

    /// <summary>
    /// Proveri da li se biblioteka i ova strana slazu.
    /// </summary>
    /// <remarks>
    /// Zove se JEDNOM, pri pokretanju. Nepoklapanje je jasna poruka umesto
    /// rusenja na prvom pozivu sa promenjenim potpisom - a to rusenje bi se
    /// desilo kod korisnika i izgledalo kao pokvaren skener.
    /// </remarks>
    public static void CheckAbiVersion()
    {
        uint actual = NativeAbiVersion;
        if (actual != ExpectedAbiVersion)
        {
            throw new AbiMismatchException(ExpectedAbiVersion, actual);
        }
    }

    // --- otvaranje ------------------------------------------------------

    public static Scanner Open(ScannerOptions options)
    {
        ArgumentNullException.ThrowIfNull(options);
        if (options.RequestedSafetyLevel is < 1 or > 5)
        {
            throw new ArgumentOutOfRangeException(nameof(options),
                "RequestedSafetyLevel mora biti 1..5.");
        }

        NativeMethods.OpenOptionsInit(out NativeMethods.OpenOptions native);
        native.Transport = (NativeMethods.Transport)options.Transport;
        native.RequestedSafetyLevel = options.RequestedSafetyLevel;
        native.AcquireTimeoutMs = (uint)Math.Max(0, options.AcquireTimeout.TotalMilliseconds);
        native.RecordTrace = options.RecordTrace ? 1 : 0;

        // Ime klijenta se drzi zivim dok poziv traje. Marshal.StringToCoTaskMemUTF8
        // alocira native bafer koji MI oslobadjamo - zato finally, a ne nada.
        IntPtr name = Marshal.StringToCoTaskMemUTF8(options.ClientName);
        try
        {
            native.ClientName = name;
            NativeMethods.Status status = NativeMethods.Open(native, out ScannerHandle handle);

            if (status != NativeMethods.Status.Ok)
            {
                handle.Dispose();

                // Handle-a jos nema, pa se poruka trazi bez njega.
                string detail = Utf8.Read((buffer, capacity) =>
                    NativeMethods.LastOpenError(IntPtr.Zero, buffer, capacity));
                throw new ScannerException((ScanStatus)status, detail);
            }
            return new Scanner(handle);
        }
        finally
        {
            Marshal.FreeCoTaskMem(name);
        }
    }

    // --- stanje ---------------------------------------------------------

    public ScannerState State
    {
        get
        {
            ThrowIfDisposed();
            return (ScannerState)NativeMethods.State(_handle);
        }
    }

    public int EffectiveSafetyLevel
    {
        get
        {
            ThrowIfDisposed();
            return NativeMethods.EffectiveSafetyLevel(_handle);
        }
    }

    /// <summary>Ko drzi uredjaj, kada je zauzet. Prazno ako se ne moze utvrditi.</summary>
    public string CurrentOwner
    {
        get
        {
            ThrowIfDisposed();
            return Utf8.Read((buffer, capacity) =>
                NativeMethods.CurrentOwner(_handle, buffer, capacity));
        }
    }

    public void Identify() => Check(NativeMethods.Identify(_handle));

    public void Begin() => Check(NativeMethods.Begin(_handle));

    public void End() => Check(NativeMethods.End(_handle));

    // --- dnevnik --------------------------------------------------------

    /// <summary>
    /// Primaj redove dnevnika. <c>null</c> iskljucuje.
    /// </summary>
    /// <remarks>
    /// POZIVA SE SA RADNE NITI. Sve sto dira UI mora sa ovog mesta preci na
    /// dispecersku nit samo - ovaj sloj to ne radi za pozivaoca, jer ne zna
    /// kakav UI stoji iznad njega.
    /// </remarks>
    public unsafe void SetLog(Action<int, string>? log)
    {
        ThrowIfDisposed();

        // Stari se otkaci PRE nego sto se novi zakaci, i tek onda oslobodi.
        // Obrnut redosled bi native strani ostavio pokazivac na oslobodjen
        // GCHandle - tacno onaj otkaz koji ovaj sloj postoji da spreci.
        if (log is null)
        {
            NativeMethods.SetLog(_handle, null, IntPtr.Zero);
            ReleaseLogPin();
            return;
        }

        var pin = new Callbacks.Pin(new Callbacks.Context { Log = log });
        NativeMethods.SetLog(_handle, &Callbacks.Log, pin.Pointer);

        ReleaseLogPin();
        _logPin = pin;
        _logPinned = true;
    }

    private void ReleaseLogPin()
    {
        if (_logPinned)
        {
            _logPin.Dispose();
            _logPinned = false;
        }
    }

    // --- operacije koje traju -------------------------------------------

    /// <summary>
    /// Upali lampu i sacekaj zagrevanje. Trazi nivo 2.
    /// </summary>
    /// <param name="progress">
    /// Napredak 0..100; vrati <c>false</c> da prekines. Poziva se sa RADNE NITI.
    /// </param>
    public ScanStatus Warmup(TimeSpan warmup, Func<int, bool>? progress = null)
    {
        ThrowIfDisposed();
        return WithProgress(progress, (pin) =>
            RunWarmup((uint)Math.Max(0, warmup.TotalMilliseconds), pin));
    }

    private unsafe NativeMethods.Status RunWarmup(uint ms, IntPtr user) =>
        NativeMethods.Warmup(_handle, ms, &Callbacks.Progress, user);

    /// <summary>
    /// Vrati glavu na pocetnu poziciju. Trazi nivo 3.
    /// </summary>
    /// <remarks>
    /// Obavezno posle <see cref="ScanStatus.TransportLost"/>: pozicija glave je
    /// tada nepoznata i nijedna druga operacija ne sme krenuti.
    /// </remarks>
    public ScanStatus Home(Func<int, bool>? progress = null)
    {
        ThrowIfDisposed();
        return WithProgress(progress, RunHome);
    }

    private unsafe NativeMethods.Status RunHome(IntPtr user) =>
        NativeMethods.Home(_handle, &Callbacks.Progress, user);

    /// <summary>
    /// Prekini sve u letu. JEDINA metoda koja sme iz druge niti.
    /// </summary>
    public void Cancel()
    {
        if (_disposed)
        {
            return;
        }
        NativeMethods.Cancel(_handle);
    }

    // --- skeniranje -----------------------------------------------------

    /// <summary>
    /// Izracunaj sta bi se desilo, BEZ diranja uredjaja.
    /// </summary>
    /// <remarks>
    /// Staticno je namerno: ceo racun radi i kada skenera nema, pa aplikacija
    /// moze pokazati velicinu slike pre nego sto se ista pomeri.
    /// </remarks>
    public static ScanGeometry Plan(ScanSettings settings)
    {
        NativeMethods.ScanRequest request = ToNative(settings);
        NativeMethods.Status status =
            NativeMethods.PlanScan(IntPtr.Zero, request, out NativeMethods.ScanInfo info);

        if (status != NativeMethods.Status.Ok)
        {
            throw new ScannerException((ScanStatus)status, string.Empty);
        }
        return ToGeometry(info);
    }

    /// <summary>Pokreni prolaz. Trazi nivo 5.</summary>
    public ScanGeometry ScanBegin(ScanSettings settings)
    {
        ThrowIfDisposed();
        NativeMethods.ScanRequest request = ToNative(settings);
        Check(NativeMethods.ScanBegin(_handle, request, out NativeMethods.ScanInfo info));
        return ToGeometry(info);
    }

    /// <summary>Sledeci gotov red.</summary>
    public ScanLineResult ScanReadLine(Span<byte> line)
    {
        ThrowIfDisposed();
        NativeMethods.Status status =
            NativeMethods.ScanReadLine(_handle, line, (uint)line.Length, out int done);

        // Otkazivanje se proverava PRE Check-a. Check ga guta kao "nije
        // greska", a to je tacno - ali slika posle njega jeste nepotpuna, i to
        // pozivalac mora saznati.
        if (status == NativeMethods.Status.Cancelled)
        {
            return ScanLineResult.Cancelled;
        }
        Check(status);
        return done == 0 ? ScanLineResult.Delivered : ScanLineResult.Complete;
    }

    /// <summary>
    /// Zatvori prolaz. Zove se i posle greske i posle otkazivanja.
    /// </summary>
    /// <remarks>
    /// Prolaz koji se ne zatvori ostavlja cip da skenira. Zato ovo ide u
    /// <c>finally</c>, i zato ne baca kada prolaza nije ni bilo.
    /// </remarks>
    public ScanStatus ScanEnd()
    {
        if (_disposed)
        {
            return ScanStatus.Ok;
        }
        return (ScanStatus)NativeMethods.ScanEnd(_handle);
    }

    // --- trag -----------------------------------------------------------

    /// <summary>Koliko je transfera zabelezeno. Nula ako snimanje nije ukljuceno.</summary>
    public int TraceCount
    {
        get
        {
            ThrowIfDisposed();
            return NativeMethods.TraceCount(_handle);
        }
    }

    /// <summary>Ispisi zabelezene transfere u fajl.</summary>
    public void WriteTrace(string path)
    {
        ThrowIfDisposed();
        ArgumentException.ThrowIfNullOrWhiteSpace(path);
        Check(NativeMethods.WriteTrace(_handle, path));
    }

    // --- unutrasnje -----------------------------------------------------

    private static NativeMethods.ScanRequest ToNative(ScanSettings settings)
    {
        ArgumentNullException.ThrowIfNull(settings);

        NativeMethods.ScanRequestInit(out NativeMethods.ScanRequest request);
        request.Resolution = settings.Resolution;
        request.ColorMode = (NativeMethods.ColorMode)settings.ColorMode;
        request.BitsPerChannel = settings.BitsPerChannel;
        request.Left = settings.Left;
        request.Top = settings.Top;
        request.Width = settings.Width;
        request.Height = settings.Height;
        request.Gamma = settings.Gamma;
        request.AllowUnqualified = settings.AllowUnqualified ? 1 : 0;
        return request;
    }

    private static ScanGeometry ToGeometry(in NativeMethods.ScanInfo info) => new(
        info.WidthPixels, info.Lines, info.BitsPerChannel, info.Channels,
        (int)info.BytesPerLine, info.NativeResolution, info.ShadingApplied != 0);

    private ScanStatus WithProgress(Func<int, bool>? progress,
                                    Func<IntPtr, NativeMethods.Status> body)
    {
        if (progress is null)
        {
            NativeMethods.Status plain = body(IntPtr.Zero);
            Check(plain);
            return (ScanStatus)plain;
        }

        using var pin = new Callbacks.Pin(new Callbacks.Context { Progress = progress });
        NativeMethods.Status status = body(pin.Pointer);

        // Izuzetak iz korisnikovog callback-a je zapamcen umesto da srusi
        // proces. Ovde je prvo mesto na kome ga sme baciti dalje - i ide PRE
        // provere ishoda, jer je on pravi uzrok prekida.
        if (pin.Target?.Failure is { } failure)
        {
            throw failure;
        }

        Check(status);
        return (ScanStatus)status;
    }

    private void Check(NativeMethods.Status status)
    {
        if (status == NativeMethods.Status.Ok)
        {
            return;
        }

        // Otkazivanje NIJE izuzetak: korisnik koji je pritisnuo "Prekini" nije
        // naisao na gresku nego je dobio ono sto je trazio.
        if (status == NativeMethods.Status.Cancelled)
        {
            return;
        }

        string detail = Utf8.Read((buffer, capacity) =>
            NativeMethods.LastError(_handle, buffer, capacity));
        uint win32 = NativeMethods.LastWin32(_handle);
        throw new ScannerException((ScanStatus)status, detail, win32);
    }

    private void ThrowIfDisposed() => ObjectDisposedException.ThrowIf(_disposed, this);

    public void Dispose()
    {
        if (_disposed)
        {
            return;
        }
        _disposed = true;

        // Redosled: prvo se native strani oduzme pokazivac na callback, pa se
        // tek onda oslobadja GCHandle, pa se na kraju zatvara uredjaj.
        try
        {
            NativeMethods.SetLogNull(_handle);
        }
        catch (DllNotFoundException)
        {
            // Biblioteka je vec otisla; nema kome da se javi.
        }
        ReleaseLogPin();
        _handle.Dispose();
    }
}
