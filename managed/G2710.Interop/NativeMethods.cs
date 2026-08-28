using System;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

namespace G2710.Interop;

/// <summary>
/// Doslovan prevod <c>native/abi/g2710_abi.h</c>. Nista vise.
/// </summary>
/// <remarks>
/// Ovde nema nijedne odluke - sve sto je odluka stoji u <see cref="Scanner"/>.
/// Razlog je isti kao svuda u projektu: sloj koji se moze pogresiti mora biti
/// odvojen od sloja koji se ne moze testirati.
///
/// Ono sto se OVDE moze pogresiti je raspored struktura, i to se ne vidi ni u
/// jednom testu ponasanja - vidi se kao smece u poljima. Zato postoji
/// InteropLayoutTests, koji pamti iste brojeve kao
/// tests/unit/abi_stability_test.cpp na C++ strani. Dve strane, isti brojevi,
/// dva nezavisna zapisa.
///
/// LibraryImport, ne DllImport: marshalling se generise u vreme prevodjenja, pa
/// se greske vide kao greske prevoda umesto kao rusenje pri prvom pozivu.
/// </remarks>
internal static partial class NativeMethods
{
    internal const string Library = "G2710.Native";

    // --- ishodi ---------------------------------------------------------
    //
    // Brojevi su DEO UGOVORA i moraju se poklapati sa g2710_abi.h.
    internal enum Status
    {
        Ok = 0,
        NotOpen = 1,
        Timeout = 2,
        ShortTransfer = 3,
        Stalled = 4,
        Cancelled = 5,
        TransportLost = 6,
        DeviceNotFound = 7,
        DeviceError = 8,
        Busy = 9,
        SafetyViolation = 10,
        NotImplemented = 11,
        InvalidArgument = 12,
        InvalidState = 13,
        Internal = 14,
    }

    internal enum DeviceState
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

    internal enum ColorMode
    {
        Color = 0,
        Gray = 1,
        Lineart = 2,
    }

    internal enum Transport
    {
        UsbScan = 0,
        Simulator = 1,
    }

    // --- strukture ------------------------------------------------------
    //
    // LayoutKind.Sequential i tacni tipovi. `Size` je prvo polje u svakoj i
    // mora se popuniti pre poziva - ABI po njemu zna koliko je polja
    // popunjeno.

    [StructLayout(LayoutKind.Sequential)]
    internal struct OpenOptions
    {
        public uint Size;
        public Transport Transport;
        public int RequestedSafetyLevel;

        /// <summary>UTF-8, NUL-terminisano. Vlasnik memorije je pozivalac.</summary>
        public IntPtr ClientName;

        public uint AcquireTimeoutMs;
        public int RecordTrace;
    }

    [StructLayout(LayoutKind.Sequential)]
    internal struct ScanRequest
    {
        public uint Size;
        public int Resolution;
        public ColorMode ColorMode;
        public int BitsPerChannel;
        public int Left;
        public int Top;
        public int Width;
        public int Height;
        public double Gamma;
        public int AllowUnqualified;
    }

    [StructLayout(LayoutKind.Sequential)]
    internal struct ScanInfo
    {
        public uint Size;
        public int WidthPixels;
        public int Lines;
        public int BitsPerChannel;
        public int Channels;
        public uint BytesPerLine;
        public int NativeResolution;
        public int ShadingApplied;
    }

    // --- ugovor ---------------------------------------------------------

    [LibraryImport(Library, EntryPoint = "g2710_abi_version")]
    [UnmanagedCallConv(CallConvs = [typeof(CallConvCdecl)])]
    internal static partial uint AbiVersion();

    [LibraryImport(Library, EntryPoint = "g2710_status_name")]
    [UnmanagedCallConv(CallConvs = [typeof(CallConvCdecl)])]
    internal static partial IntPtr StatusName(Status status);

    [LibraryImport(Library, EntryPoint = "g2710_build_safety_ceiling")]
    [UnmanagedCallConv(CallConvs = [typeof(CallConvCdecl)])]
    internal static partial int BuildSafetyCeiling();

    [LibraryImport(Library, EntryPoint = "g2710_motor_path_compiled")]
    [UnmanagedCallConv(CallConvs = [typeof(CallConvCdecl)])]
    internal static partial int MotorPathCompiled();

    // --- otvaranje ------------------------------------------------------

    [LibraryImport(Library, EntryPoint = "g2710_open_options_init")]
    [UnmanagedCallConv(CallConvs = [typeof(CallConvCdecl)])]
    internal static partial void OpenOptionsInit(out OpenOptions options);

    [LibraryImport(Library, EntryPoint = "g2710_open")]
    [UnmanagedCallConv(CallConvs = [typeof(CallConvCdecl)])]
    internal static partial Status Open(in OpenOptions options, out ScannerHandle device);

    /// <summary>
    /// Zove ga SAMO <see cref="ScannerHandle.ReleaseHandle"/>.
    /// </summary>
    /// <remarks>
    /// Uzima sirov IntPtr, ne SafeHandle: SafeHandle koji zatvara sam sebe je
    /// rekurzija koju runtime ne dozvoljava.
    /// </remarks>
    [LibraryImport(Library, EntryPoint = "g2710_close")]
    [UnmanagedCallConv(CallConvs = [typeof(CallConvCdecl)])]
    internal static partial void Close(IntPtr device);

    // --- greske ---------------------------------------------------------

    [LibraryImport(Library, EntryPoint = "g2710_last_error")]
    [UnmanagedCallConv(CallConvs = [typeof(CallConvCdecl)])]
    internal static partial int LastError(ScannerHandle device,
                                          Span<byte> buffer, int capacity);

    /// <summary>Varijanta bez handle-a: greska neuspelog otvaranja.</summary>
    [LibraryImport(Library, EntryPoint = "g2710_last_error")]
    [UnmanagedCallConv(CallConvs = [typeof(CallConvCdecl)])]
    internal static partial int LastOpenError(IntPtr device, Span<byte> buffer,
                                              int capacity);

    [LibraryImport(Library, EntryPoint = "g2710_last_win32")]
    [UnmanagedCallConv(CallConvs = [typeof(CallConvCdecl)])]
    internal static partial uint LastWin32(ScannerHandle device);

    // --- stanje ---------------------------------------------------------

    [LibraryImport(Library, EntryPoint = "g2710_identify")]
    [UnmanagedCallConv(CallConvs = [typeof(CallConvCdecl)])]
    internal static partial Status Identify(ScannerHandle device);

    [LibraryImport(Library, EntryPoint = "g2710_begin")]
    [UnmanagedCallConv(CallConvs = [typeof(CallConvCdecl)])]
    internal static partial Status Begin(ScannerHandle device);

    [LibraryImport(Library, EntryPoint = "g2710_end")]
    [UnmanagedCallConv(CallConvs = [typeof(CallConvCdecl)])]
    internal static partial Status End(ScannerHandle device);

    [LibraryImport(Library, EntryPoint = "g2710_state")]
    [UnmanagedCallConv(CallConvs = [typeof(CallConvCdecl)])]
    internal static partial DeviceState State(ScannerHandle device);

    [LibraryImport(Library, EntryPoint = "g2710_effective_safety_level")]
    [UnmanagedCallConv(CallConvs = [typeof(CallConvCdecl)])]
    internal static partial int EffectiveSafetyLevel(ScannerHandle device);

    [LibraryImport(Library, EntryPoint = "g2710_current_owner")]
    [UnmanagedCallConv(CallConvs = [typeof(CallConvCdecl)])]
    internal static partial int CurrentOwner(ScannerHandle device, Span<byte> buffer,
                                             int capacity);

    // --- operacije koje traju -------------------------------------------
    //
    // NEUPRAVLJANI POKAZIVACI NA FUNKCIJE, ne delegati.
    //
    // Delegat prosledjen native strani mora ostati ziv dok ga ta strana moze
    // pozvati, a GC to ne zna. Klasican ishod je rusenje koje se desava retko,
    // kod korisnika, i nikada u testu. `delegate* unmanaged[Cdecl]` uklanja
    // celu tu klasu gresaka: pokazuje na staticku metodu koja se ne skuplja, a
    // stanje putuje kroz `user` kao GCHandle - koji je EKSPLICITNO ziv.

    [LibraryImport(Library, EntryPoint = "g2710_set_log")]
    [UnmanagedCallConv(CallConvs = [typeof(CallConvCdecl)])]
    internal static unsafe partial void SetLog(ScannerHandle device,
                                               delegate* unmanaged[Cdecl]<int, IntPtr, IntPtr, void> log,
                                               IntPtr user);

    /// <summary>Iskljuci dnevnik. Odvojeno da pozivalac ne mora `unsafe`.</summary>
    internal static unsafe void SetLogNull(ScannerHandle device) =>
        SetLog(device, null, IntPtr.Zero);

    [LibraryImport(Library, EntryPoint = "g2710_warmup")]
    [UnmanagedCallConv(CallConvs = [typeof(CallConvCdecl)])]
    internal static unsafe partial Status Warmup(ScannerHandle device, uint warmupMs,
                                                 delegate* unmanaged[Cdecl]<int, IntPtr, int> progress,
                                                 IntPtr user);

    [LibraryImport(Library, EntryPoint = "g2710_home")]
    [UnmanagedCallConv(CallConvs = [typeof(CallConvCdecl)])]
    internal static unsafe partial Status Home(ScannerHandle device,
                                               delegate* unmanaged[Cdecl]<int, IntPtr, int> progress,
                                               IntPtr user);

    [LibraryImport(Library, EntryPoint = "g2710_cancel")]
    [UnmanagedCallConv(CallConvs = [typeof(CallConvCdecl)])]
    internal static partial void Cancel(ScannerHandle device);

    // --- skeniranje -----------------------------------------------------

    [LibraryImport(Library, EntryPoint = "g2710_scan_request_init")]
    [UnmanagedCallConv(CallConvs = [typeof(CallConvCdecl)])]
    internal static partial void ScanRequestInit(out ScanRequest request);

    [LibraryImport(Library, EntryPoint = "g2710_plan_scan")]
    [UnmanagedCallConv(CallConvs = [typeof(CallConvCdecl)])]
    internal static partial Status PlanScan(IntPtr device, in ScanRequest request,
                                            out ScanInfo info);

    [LibraryImport(Library, EntryPoint = "g2710_scan_begin")]
    [UnmanagedCallConv(CallConvs = [typeof(CallConvCdecl)])]
    internal static partial Status ScanBegin(ScannerHandle device, in ScanRequest request,
                                             out ScanInfo info);

    [LibraryImport(Library, EntryPoint = "g2710_scan_read_line")]
    [UnmanagedCallConv(CallConvs = [typeof(CallConvCdecl)])]
    internal static partial Status ScanReadLine(ScannerHandle device, Span<byte> buffer,
                                                uint capacity, out int done);

    [LibraryImport(Library, EntryPoint = "g2710_scan_end")]
    [UnmanagedCallConv(CallConvs = [typeof(CallConvCdecl)])]
    internal static partial Status ScanEnd(ScannerHandle device);

    // --- trag -----------------------------------------------------------

    [LibraryImport(Library, EntryPoint = "g2710_write_trace", StringMarshalling = StringMarshalling.Utf8)]
    [UnmanagedCallConv(CallConvs = [typeof(CallConvCdecl)])]
    internal static partial Status WriteTrace(ScannerHandle device, string path);

    // --- mogucnosti -----------------------------------------------------

    /// <summary>Tabela mogucnosti kao JSON. Ne trazi handle ni uredjaj.</summary>
    [LibraryImport(Library, EntryPoint = "g2710_capabilities")]
    [UnmanagedCallConv(CallConvs = [typeof(CallConvCdecl)])]
    internal static partial int Capabilities(Span<byte> buffer, int capacity);

    [LibraryImport(Library, EntryPoint = "g2710_trace_count")]
    [UnmanagedCallConv(CallConvs = [typeof(CallConvCdecl)])]
    internal static partial int TraceCount(ScannerHandle device);
}
