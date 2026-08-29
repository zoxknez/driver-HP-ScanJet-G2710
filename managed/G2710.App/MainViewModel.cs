using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Threading.Tasks;
using System.Windows;
using System.Windows.Media.Imaging;
using G2710.Interop;

namespace G2710.App;

/// <summary>Početni ekran aplikacije. Ne poziva NativeMethods direktno.</summary>
internal sealed class MainViewModel : Observable
{
    private ScannerTransport _transport = ScannerTransport.UsbScan;
    private ResolutionChoice? _resolutionChoice;
    private ScanColorMode _colorMode = ScanColorMode.Color;
    private string _statusTitle = "Spremno za proveru";
    private string _statusDetail = "Izaberite izvor, pa proverite vezu sa skenerom.";
    private string _diagnostics = "Dijagnostika još nije pokrenuta.";
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
        0 => "Nema odloženih stranica.",
        1 => "1 odložena stranica; izvoz u PDF daće 2 strane.",
        _ => $"{_pages.Count} odloženih stranica; izvoz u PDF daće {_pages.Count + 1} strana.",
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
        StatusTitle = "Stranica je odložena";
        StatusDetail = PagesSummary + " Skenirajte sledeću, pa izvezite u PDF.";
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
            Diagnostics = $"ABI: {Scanner.NativeAbiVersion >> 16}.{Scanner.NativeAbiVersion & 0xffff}\nPlafon build-a: {Scanner.BuildSafetyCeiling}\nMotorni put: {(_scanner is not null && Scanner.MotorPathCompiled ? "preveden" : "nije preveden")}\nPlan: {plan.WidthPixels} × {plan.Lines}, {plan.NativeResolution} dpi, shading: {(plan.ShadingApplied ? "da" : "ne")}";
            StatusTitle = "Veza je proverena";
            StatusDetail = plan.ShadingApplied ? "Skener je spreman." : "Skeniranje radi, ali kalibracija senzora još nije hardverski potvrđena.";
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
            StatusTitle = "Skener nije spreman";
            StatusDetail = exception.Message;
            Diagnostics = exception.ToString();
        }
        finally { WriteTraceCommand.RaiseCanExecuteChanged(); }
    }

    private void WriteTrace()
    {
        if (_scanner is null) return;
        var path = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.DesktopDirectory), $"G2710-trace-{DateTime.Now:yyyyMMdd-HHmmss}.txt");
        _scanner.WriteTrace(path);
        StatusDetail = $"Trag je sačuvan: {path}";
    }

    private async Task ScanAsync()
    {
        IsScanning = true;
        StatusTitle = "Skeniranje je u toku";
        StatusDetail = "Pripremam skener…";
        try
        {
            var image = await Task.Run(() => RunScan(false)).ConfigureAwait(true);
            if (image is null)
            {
                StatusTitle = "Skeniranje je prekinuto";
                StatusDetail = "Delimična slika nije sačuvana.";
                return;
            }
            StatusTitle = "Skeniranje je završeno";
            _lastImage = image;
            Raise(nameof(HasImage));
            ExportCommand.RaiseCanExecuteChanged();
            SetPreview(image);
            StatusDetail = $"Primljeno je {image.Width} × {image.Height} piksela. Izaberite format izvoza u sledećem koraku.";
        }
        catch (ScannerException exception)
        {
            SetScannerFailure(exception);
            Diagnostics = exception.ToString();
        }
        catch (Exception exception) { StatusTitle = "Skeniranje nije uspelo"; StatusDetail = exception.Message; Diagnostics = exception.ToString(); }
        finally
        {
            IsScanning = false;
        }
    }

    private async Task PreviewAsync()
    {
        IsScanning = true;
        StatusTitle = "Preview je u toku";
        StatusDetail = "Skeniram na 150 dpi…";
        try
        {
            var image = await Task.Run(() => RunScan(true)).ConfigureAwait(true);
            if (image is null) { StatusTitle = "Preview je prekinut"; StatusDetail = "Nije sačuvana delimična slika."; return; }
            SetPreview(image);
            StatusTitle = "Preview je spreman";
            StatusDetail = "Izaberite oblast skeniranja na prikazu.";
        }
        catch (ScannerException exception) { SetScannerFailure(exception); Diagnostics = exception.ToString(); }
        catch (Exception exception) { StatusTitle = "Preview nije uspeo"; StatusDetail = exception.Message; Diagnostics = exception.ToString(); }
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
                StatusTitle = "Izvoz nije uspeo";
                StatusDetail = $"{PagesSummary} Više stranica podržava samo PDF - " +
                               "izaberite PDF ili obrišite odložene stranice.";
                return;
            }

            ImageExport.Save(_lastImage, ExportFormat, path,
                             _pages.Count > 0 ? _pages : null);

            StatusTitle = "Slika je izvezena";
            StatusDetail = _pages.Count > 0
                ? $"{path} ({_pages.Count + 1} strana)"
                : path;
        }
        catch (Exception exception) { StatusTitle = "Izvoz nije uspeo"; StatusDetail = exception.Message; }
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
                StatusTitle = "Skener nije pronađen";
                StatusDetail = "Proverite USB kabl, napajanje i da li je uređaj uključen, pa pokušajte ponovo.";
                break;
            case ScanStatus.Busy:
                StatusTitle = "Skener je zauzet";
                StatusDetail = owner is { Length: > 0 }
                    ? $"Uređaj trenutno koristi: {owner}. Završite taj posao pa pokušajte ponovo."
                    : "Drugi program trenutno koristi skener. Zatvorite ga pa pokušajte ponovo.";
                break;
            case ScanStatus.SafetyViolation:
                StatusTitle = "Radnja nije dozvoljena";
                StatusDetail = "Ovaj paket nema potreban bezbednosni plafon za traženu radnju.";
                break;
            default:
                StatusTitle = "Skeniranje nije uspelo";
                StatusDetail = exception.Message;
                break;
        }
    }
}
