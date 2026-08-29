using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Threading.Tasks;
using System.Windows;
using System.Windows.Media.Imaging;
using G2710.Interop;
using G2710.Localization;

namespace G2710.App;

/// <summary>Početni ekran aplikacije. Ne poziva NativeMethods direktno.</summary>
internal sealed class MainViewModel : Observable
{
    private ScannerTransport _transport = ScannerTransport.UsbScan;
    private ResolutionChoice? _resolutionChoice;
    private ScanColorMode _colorMode = ScanColorMode.Color;
    private string _statusTitle = Strings.Get("App_Status_Ready");
    private string _statusDetail = Strings.Get("App_Status_Ready_Detail");
    private string _diagnostics = Strings.Get("App_Diagnostics_Idle");
    private Scanner? _scanner;
    private Scanner? _activeScanner;
    private bool _isScanning;
    private BitmapSource? _previewImage;
    private ScanGeometry? _previewGeometry;
    private int _cropLeft, _cropTop, _cropWidth, _cropHeight;
    private ScanImage? _lastImage;

    // Stranice prikupljene za visestranicni PDF.
    //
    // Izvozni sloj je od pocetka primao vise stranica, ali kroz UI nije bilo
    // nijednog puta da se druga stranica doda - pa je mogucnost postojala samo
    // u kodu. Ovo je taj put.
    private readonly List<ScanImage> _pages = [];
    private ExportFormat _exportFormat = ExportFormat.Png;

    public MainViewModel()
    {
        // Rezolucije dolaze iz jezgra, ne iz spiska otkucanog u XAML-u.
        //
        // Ovde se namerno nude i one koje hardver jos nije potvrdio: aplikacija
        // je i dijagnosticki alat, a bez skenera nijedna vrednost nije
        // potvrdjena - lista bi inace bila PRAZNA. Ono sto se ne sme precutati
        // je da vrednost nije potvrdjena, i to stoji u SelectedResolutionCaveat.
        Resolutions = ResolutionChoice.From(ReadCapabilities(), includeUnqualified: true);
        _resolutionChoice = Resolutions.FirstOrDefault(r => r.Dpi == 300)
                            ?? Resolutions.FirstOrDefault();

        CheckCommand = new RelayCommand(CheckConnection);
        WriteTraceCommand = new RelayCommand(WriteTrace, () => _scanner is not null);
        ScanCommand = new RelayCommand(async () => await ScanAsync(), () => !_isScanning);
        PreviewCommand = new RelayCommand(async () => await PreviewAsync(), () => !_isScanning);
        CancelCommand = new RelayCommand(() => _activeScanner?.Cancel(), () => _isScanning);
        ExportCommand = new RelayCommand(Export, () => _lastImage is not null && !_isScanning);
        AddPageCommand = new RelayCommand(AddPage, () => _lastImage is not null && !_isScanning);
        ClearPagesCommand = new RelayCommand(ClearPages, () => _pages.Count > 0 && !_isScanning);
    }

    public ScannerTransport Transport { get => _transport; set => Set(ref _transport, value); }
    /// <summary>Sve sto drajver ume, sa statusom svake vrednosti.</summary>
    public IReadOnlyList<ResolutionChoice> Resolutions { get; }

    public ResolutionChoice? SelectedResolution
    {
        get => _resolutionChoice;
        set
        {
            if (Set(ref _resolutionChoice, value))
            {
                Raise(nameof(Resolution));
                Raise(nameof(SelectedResolutionCaveat));
                Raise(nameof(HasResolutionCaveat));
            }
        }
    }

    /// <summary>Izabrana vrednost u dpi; 300 ako lista jos nije popunjena.</summary>
    public int Resolution => _resolutionChoice?.Dpi ?? 300;

    /// <summary>
    /// Sta korisnik treba da zna o izabranoj rezoluciji. Prazno = nema sta.
    /// </summary>
    public string SelectedResolutionCaveat => _resolutionChoice?.Caveat ?? string.Empty;

    public bool HasResolutionCaveat => !string.IsNullOrEmpty(SelectedResolutionCaveat);

    /// <summary>
    /// Tabela mogucnosti. Izdvojeno da se moze zameniti u testovima - citanje
    /// iz native strane trazi ucitanu biblioteku.
    /// </summary>
    internal static Func<ScannerCapabilities> ReadCapabilities { get; set; } =
        Scanner.Capabilities;
    public ScanColorMode ColorMode { get => _colorMode; set => Set(ref _colorMode, value); }
    public string StatusTitle { get => _statusTitle; private set => Set(ref _statusTitle, value); }
    public string StatusDetail { get => _statusDetail; private set => Set(ref _statusDetail, value); }
    public string Diagnostics { get => _diagnostics; private set => Set(ref _diagnostics, value); }
    public RelayCommand CheckCommand { get; }
    public RelayCommand WriteTraceCommand { get; }
    public RelayCommand ScanCommand { get; }
    public RelayCommand PreviewCommand { get; }
    public RelayCommand CancelCommand { get; }
    public RelayCommand ExportCommand { get; }
    public BitmapSource? PreviewImage { get => _previewImage; private set => Set(ref _previewImage, value); }
    public int CropLeft { get => _cropLeft; set => Set(ref _cropLeft, Math.Max(0, value)); }
    public int CropTop { get => _cropTop; set => Set(ref _cropTop, Math.Max(0, value)); }
    public int CropWidth { get => _cropWidth; set => Set(ref _cropWidth, Math.Max(0, value)); }
    public int CropHeight { get => _cropHeight; set => Set(ref _cropHeight, Math.Max(0, value)); }
    public ExportFormat ExportFormat { get => _exportFormat; set => Set(ref _exportFormat, value); }
    public bool HasImage => _lastImage is not null;

    public RelayCommand AddPageCommand { get; }
    public RelayCommand ClearPagesCommand { get; }

    /// <summary>Koliko je stranica odlozeno za visestranicni PDF.</summary>
    public int PageCount => _pages.Count;

    /// <summary>Sta pise pored dugmeta, da se broj ne mora pogadjati.</summary>
    public string PagesSummary => _pages.Count switch
    {
        0 => Strings.Get("App_Pages_None"),
        1 => Strings.Get("App_Pages_One"),
        _ => Strings.Format("App_Pages_Many", _pages.Count, _pages.Count + 1),
    };

    /// <summary>
    /// Odloži tekuću sliku kao stranicu.
    /// </summary>
    /// <remarks>
    /// Samo PDF nosi više stranica; ostali formati bi tiho izgubili sve osim
    /// prve, pa se izvoz u njih odbija dok ima odloženih.
    /// </remarks>
    internal void AddPage()
    {
        if (_lastImage is null)
        {
            return;
        }
        _pages.Add(_lastImage);
        RaisePageState();
        StatusTitle = Strings.Get("App_Pages_Added");
        StatusDetail = Strings.Format("App_Pages_Added_Detail", PagesSummary);
    }

    /// <summary>
    /// Postavi sliku bez skeniranja. Samo za testove.
    /// </summary>
    /// <remarks>
    /// Odlaganje stranica ne zavisi od uredjaja, a bez ovoga bi svaki test
    /// morao da provoza ceo prolaz - pa bi merio skeniranje umesto brojanja
    /// stranica.
    /// </remarks>
    internal void SetLastImageForTest(ScanImage image)
    {
        _lastImage = image;
        Raise(nameof(HasImage));
        AddPageCommand.RaiseCanExecuteChanged();
        ExportCommand.RaiseCanExecuteChanged();
    }

    internal void ClearPages()
    {
        _pages.Clear();
        RaisePageState();
        StatusDetail = PagesSummary;
    }

    private void RaisePageState()
    {
        Raise(nameof(PageCount));
        Raise(nameof(PagesSummary));
        Raise(nameof(HasPages));
        AddPageCommand.RaiseCanExecuteChanged();
        ClearPagesCommand.RaiseCanExecuteChanged();
    }

    public bool HasPages => _pages.Count > 0;
    public bool IsScanning { get => _isScanning; private set { if (Set(ref _isScanning, value)) { Raise(nameof(CanScan)); ScanCommand.RaiseCanExecuteChanged(); PreviewCommand.RaiseCanExecuteChanged(); CancelCommand.RaiseCanExecuteChanged(); ExportCommand.RaiseCanExecuteChanged(); } } }
    public bool CanScan => !IsScanning;

    /// <summary>
    /// Verzija ovog programa.
    /// </summary>
    /// <remarks>
    /// Bez nje se sa tuđeg računara ne može utvrditi koji je build tamo. Za
    /// drajver koji se otklanja na daljinu to je isti problem kao trag koji ne
    /// beleži identitet uređaja: izveštaj stigne, a ne zna se na šta se odnosi.
    ///
    /// Broj dolazi iz korenskog VERSION fajla, istog iz koga ga uzima i MSI.
    /// </remarks>
    internal static string AppVersion =>
        typeof(MainViewModel).Assembly.GetName().Version?.ToString(3) ?? "nepoznata";

    /// <summary>Callback iz native skena stiže sa radne niti.</summary>
    public Action<Action> OnUiThread { get; set; } = action =>
    {
        var dispatcher = Application.Current?.Dispatcher;
        if (dispatcher is null || dispatcher.CheckAccess()) action();
        else dispatcher.Invoke(action);
    };

    private void CheckConnection()
    {
        _scanner?.Dispose();
        _scanner = null;
        try
        {
            Scanner.CheckAbiVersion();
            _scanner = Scanner.Open(new ScannerOptions { Transport = Transport, RequestedSafetyLevel = 1, ClientName = "G2710.App", RecordTrace = true });
            _scanner.Identify();
            _scanner.Begin();
            ScanGeometry plan;
            try
            {
                plan = Scanner.Plan(new ScanSettings { Resolution = Resolution, ColorMode = ColorMode, AllowUnqualified = Transport == ScannerTransport.Simulator });
            }
            finally
            {
                // Check je read-only: ne sme da ostavi Global DataSession i
                // blokira sledeci preview ili drugog klijenta.
                _scanner.End();
            }
            Diagnostics = BuildDiagnostics(plan);
            StatusTitle = Strings.Get("App_Status_Checked");
            StatusDetail = Strings.Get(plan.ShadingApplied
                ? "App_Status_Checked_Ready" : "App_Status_Checked_NoShading");
        }
        catch (ScannerException exception)
        {
            var owner = exception.Status == ScanStatus.Busy ? TryCurrentOwner() : null;
            _scanner?.Dispose(); _scanner = null;
            SetScannerFailure(exception, owner);
            Diagnostics = exception.ToString();
        }
        catch (Exception exception)
        {
            _scanner?.Dispose(); _scanner = null;
            StatusTitle = Strings.Get("App_Status_NotReady");
            StatusDetail = exception.Message;
            Diagnostics = exception.ToString();
        }
        finally { WriteTraceCommand.RaiseCanExecuteChanged(); }
    }

    /// <summary>
    /// Dijagnostika, na jeziku koji je korisnik izabrao.
    /// </summary>
    /// <remarks>
    /// Nazivi polja se prevode, vrednosti ne: verzija i plafon su brojevi, a ne
    /// recenice. Ono sto se salje nazad kao izvestaj mora ostati citljivo i
    /// onome ko ne govori jezik korisnika.
    /// </remarks>
    private static string BuildDiagnostics(ScanGeometry plan)
    {
        uint abi = Scanner.NativeAbiVersion;
        string motor = Strings.Get(Scanner.MotorPathCompiled
            ? "App_Diagnostics_MotorPath_Yes" : "App_Diagnostics_MotorPath_No");
        string shading = Strings.Get(plan.ShadingApplied ? "Common_Yes" : "Common_No");

        return string.Join('\n', new[]
        {
            Strings.Get("App_Diagnostics_Version") + ": " + AppVersion,
            Strings.Get("App_Diagnostics_Abi") + ": " + (abi >> 16) + "." + (abi & 0xffff),
            Strings.Get("App_Diagnostics_Ceiling") + ": " + Scanner.BuildSafetyCeiling,
            Strings.Get("App_Diagnostics_MotorPath") + ": " + motor,
            Strings.Get("App_Diagnostics_Plan") + ": " + plan.WidthPixels + " × " +
                plan.Lines + ", " + plan.NativeResolution + " dpi, " +
                Strings.Get("App_Diagnostics_Shading") + ": " + shading,
        });
    }

    private void WriteTrace()
    {
        if (_scanner is null) return;
        var path = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.DesktopDirectory), $"G2710-trace-{DateTime.Now:yyyyMMdd-HHmmss}.txt");
        _scanner.WriteTrace(path);
        StatusDetail = Strings.Format("App_Status_TraceSaved", path);
    }

    private async Task ScanAsync()
    {
        IsScanning = true;
        StatusTitle = Strings.Get("App_Status_Scanning");
        StatusDetail = Strings.Get("App_Status_Preparing");
        try
        {
            var image = await Task.Run(() => RunScan(false)).ConfigureAwait(true);
            if (image is null)
            {
                StatusTitle = Strings.Get("App_Status_Scan_Cancelled");
                StatusDetail = Strings.Get("App_Status_Scan_Cancelled_Detail");
                return;
            }
            StatusTitle = Strings.Get("App_Status_Scan_Done");
            _lastImage = image;
            Raise(nameof(HasImage));
            ExportCommand.RaiseCanExecuteChanged();
            SetPreview(image);
            StatusDetail = Strings.Format("App_Status_Received", image.Width, image.Height);
        }
        catch (ScannerException exception)
        {
            SetScannerFailure(exception);
            Diagnostics = exception.ToString();
        }
        catch (Exception exception) { StatusTitle = Strings.Get("App_Status_Scan_Failed"); StatusDetail = exception.Message; Diagnostics = exception.ToString(); }
        finally
        {
            IsScanning = false;
        }
    }

    private async Task PreviewAsync()
    {
        IsScanning = true;
        StatusTitle = Strings.Get("App_Status_Preview_Running");
        StatusDetail = Strings.Get("App_Status_Preview_Detail");
        try
        {
            var image = await Task.Run(() => RunScan(true)).ConfigureAwait(true);
            if (image is null) { StatusTitle = Strings.Get("App_Status_Preview_Cancelled"); StatusDetail = Strings.Get("App_Status_Scan_Cancelled_Detail"); return; }
            SetPreview(image);
            StatusTitle = Strings.Get("App_Status_Preview_Ready");
            StatusDetail = Strings.Get("App_Status_Preview_Ready_Detail");
        }
        catch (ScannerException exception) { SetScannerFailure(exception); Diagnostics = exception.ToString(); }
        catch (Exception exception) { StatusTitle = Strings.Get("App_Status_Preview_Failed"); StatusDetail = exception.Message; Diagnostics = exception.ToString(); }
        finally { IsScanning = false; }
    }

    private void SetPreview(ScanImage image)
    {
        _previewGeometry = new ScanGeometry(image.Width, image.Height, image.BitsPerChannel,
            image.Channels, image.Stride, 150, true);
        var bitmap = ImageExport.CreateBitmap(image);
        bitmap.Freeze();
        PreviewImage = bitmap;
    }

    private void Export()
    {
        if (_lastImage is null) return;
        try
        {
            var extension = ExportFormat switch { ExportFormat.Png => "png", ExportFormat.Jpeg => "jpg", ExportFormat.Tiff8 or ExportFormat.Tiff16 => "tif", ExportFormat.Pdf => "pdf", _ => throw new ArgumentOutOfRangeException() };
            var path = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.DesktopDirectory), $"G2710-scan-{DateTime.Now:yyyyMMdd-HHmmss}.{extension}");
            if (_pages.Count > 0 && ExportFormat != ExportFormat.Pdf)
            {
                // Tih gubitak stranica je gori od odbijenog izvoza: korisnik bi
                // dobio fajl koji izgleda ispravno a nosi samo poslednju sliku.
                StatusTitle = Strings.Get("App_Status_Export_Failed");
                StatusDetail = Strings.Format("App_Pages_OnlyPdf", PagesSummary);
                return;
            }

            ImageExport.Save(_lastImage, ExportFormat, path,
                             _pages.Count > 0 ? _pages : null);

            StatusTitle = Strings.Get("App_Status_Exported");
            StatusDetail = _pages.Count > 0
                ? Strings.Format("App_Export_Pages", path, _pages.Count + 1)
                : path;
        }
        catch (Exception exception) { StatusTitle = Strings.Get("App_Status_Export_Failed"); StatusDetail = exception.Message; }
    }

    private ScanImage? RunScan(bool preview)
    {
        using var scanner = Scanner.Open(new ScannerOptions
        {
            Transport = Transport,
            RequestedSafetyLevel = 5,
            ClientName = "G2710.App",
            RecordTrace = true,
        });
        _activeScanner = scanner;
        var begun = false;
        try
        {
            scanner.Identify();
            scanner.Begin();
            begun = true;
            scanner.Warmup(TimeSpan.Zero);
            var settings = new ScanSettings
            {
                Resolution = preview ? 150 : Resolution,
                ColorMode = ColorMode,
                AllowUnqualified = Transport == ScannerTransport.Simulator,
            };
            if (!preview && _previewGeometry is not null && CropWidth > 0 && CropHeight > 0)
            {
                var target = Scanner.Plan(settings);
                settings = CropTransform.ToScanSettings(new CropTransform.Rect(CropLeft, CropTop, CropWidth, CropHeight),
                    _previewGeometry, target, settings);
            }
            return ScanWorkflow.Run(scanner, settings, percent => OnUiThread(() => StatusDetail = $"Skeniranje: {percent}%"));
        }
        finally
        {
            _activeScanner = null;
            // Identify/Begin mogu pasti pre prelaza u Idle. End tada nije
            // legalan i ne sme prekriti stvarni razlog greske korisniku.
            if (begun) scanner.End();
        }
    }

    private string? TryCurrentOwner()
    {
        try { return string.IsNullOrWhiteSpace(_scanner?.CurrentOwner) ? null : _scanner.CurrentOwner; }
        catch { return null; }
    }

    private void SetScannerFailure(ScannerException exception, string? owner = null)
    {
        switch (exception.Status)
        {
            case ScanStatus.DeviceNotFound:
            case ScanStatus.TransportLost:
                StatusTitle = Strings.Get("Err_DeviceNotFound");
                StatusDetail = Strings.Get("App_Fail_CheckCable");
                break;
            case ScanStatus.Busy:
                StatusTitle = Strings.Get("Err_Busy");
                StatusDetail = owner is { Length: > 0 }
                    ? Strings.Format("App_Fail_BusyOwner", owner)
                    : Strings.Get("App_Fail_BusyUnknown");
                break;
            case ScanStatus.SafetyViolation:
                StatusTitle = Strings.Get("Err_SafetyViolation");
                StatusDetail = Strings.Get("App_Fail_Ceiling");
                break;
            default:
                StatusTitle = Strings.Get("App_Status_Scan_Failed");
                StatusDetail = exception.Message;
                break;
        }
    }
}
