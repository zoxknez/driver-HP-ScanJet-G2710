using System;
using System.Collections.ObjectModel;
using System.IO;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;
using System.Windows.Input;
using G2710.Localization;
using G2710.Qualification.Models;
using G2710.Qualification.Services;

namespace G2710.Qualification.ViewModels;

/// <summary>
/// Ceo tok wizarda.
/// </summary>
/// <remarks>
/// Cetiri koraka, i svaki ima svoja stanja. Sve odluke o tome sta se prikazuje
/// su ovde; pogledi samo vezuju vrednosti. Zato se ceo tok moze testirati bez
/// otvaranja prozora - vidi managed/G2710.Qualification.Tests.
/// </remarks>
public sealed class MainViewModel : Observable
{
    private readonly Func<string, QualificationRunner> _runnerFactory;
    private readonly Func<ToolLocator.Result> _locate;

    private CancellationTokenSource? _cancellation;

    /// <summary>
    /// Prebacuje posao na nit koja sme da dira UI.
    /// </summary>
    /// <remarks>
    /// Ispis alata stize sa thread-pool niti (Process.OutputDataReceived), a
    /// ObservableCollection vezan za prikaz sme se menjati SAMO sa dispecerske.
    /// Bez ovoga se prozor rusi cim alat ispise prvi red - i to se ne vidi ni u
    /// jednom testu, jer u testu nema dispecera.
    ///
    /// Izdvojeno kao polje da bi testovi mogli da rade bez WPF aplikacije.
    /// </remarks>
    public Action<Action> OnUiThread { get; set; } = action =>
    {
        var dispatcher = System.Windows.Application.Current?.Dispatcher;
        if (dispatcher is null || dispatcher.CheckAccess())
        {
            action();
        }
        else
        {
            dispatcher.Invoke(action);
        }
    };

    public MainViewModel(
        Func<ToolLocator.Result>? locate = null,
        Func<string, QualificationRunner>? runnerFactory = null)
    {
        _locate = locate ?? (() => ToolLocator.Locate());
        _runnerFactory = runnerFactory ?? (path => new QualificationRunner(path));

        StartCommand = new RelayCommand(async () => await StartAsync().ConfigureAwait(true),
                                        () => State != ScreenState.Busy);
        CancelCommand = new RelayCommand(Cancel, () => State == ScreenState.Busy);
        RetryCommand = new RelayCommand(async () => await StartAsync().ConfigureAwait(true),
                                        () => State != ScreenState.Busy);
        NextCommand = new RelayCommand(GoNext, CanGoNext);
        BackCommand = new RelayCommand(GoBack, CanGoBack);
        SaveReportCommand = new RelayCommand(
            async () => await SaveAndPackageAsync().ConfigureAwait(true),
            () => Report is not null && State != ScreenState.Busy);
        OpenReportFolderCommand = new RelayCommand(OpenReportFolder,
                                                   () => DeliverablePath is not null);
    }

    // --- korak i stanje ------------------------------------------------------

    private WizardStep _step = WizardStep.Welcome;
    public WizardStep Step
    {
        get => _step;
        private set
        {
            if (Set(ref _step, value))
            {
                Raise(nameof(IsWelcome));
                Raise(nameof(IsRunning));
                Raise(nameof(IsQuestions));
                Raise(nameof(IsResults));
                Raise(nameof(Step1Done));
                Raise(nameof(Step2Done));
                Raise(nameof(Step3Done));
                Raise(nameof(StepTitle));
                Raise(nameof(StepSubtitle));
                RefreshCommands();
            }
        }
    }

    public bool IsWelcome => Step == WizardStep.Welcome;
    public bool IsRunning => Step == WizardStep.Running;
    public bool IsQuestions => Step == WizardStep.Questions;
    public bool IsResults => Step == WizardStep.Results;

    // Korak je ZAVRSEN kada je tekuci korak iza njega.
    //
    // Vezivanje za "jesmo li na sledecem koraku" izgleda isto dok se ide
    // redom, a pogresi cim se preskoci jedan - i tada se zavrseni korak vrati
    // u broj. Bas to se i desilo koraku 2 kada nema pitanja.
    public bool Step1Done => Step > WizardStep.Welcome;
    public bool Step2Done => Step > WizardStep.Running;
    public bool Step3Done => Step > WizardStep.Questions;

    private ScreenState _state = ScreenState.Idle;
    public ScreenState State
    {
        get => _state;
        private set
        {
            if (Set(ref _state, value))
            {
                Raise(nameof(IsIdle));
                Raise(nameof(IsBusy));
                Raise(nameof(IsReady));
                Raise(nameof(IsEmpty));
                Raise(nameof(IsFailed));
                RefreshCommands();
            }
        }
    }

    public bool IsIdle => State == ScreenState.Idle;
    public bool IsBusy => State == ScreenState.Busy;
    public bool IsReady => State == ScreenState.Ready;
    public bool IsEmpty => State == ScreenState.Empty;
    public bool IsFailed => State == ScreenState.Failed;

    public string StepTitle => Step switch
    {
        WizardStep.Welcome => Strings.Get("Wiz_Head_Welcome"),
        WizardStep.Running => Strings.Get("Wiz_Head_Running"),
        WizardStep.Questions => Strings.Get("Wiz_Head_Questions"),
        WizardStep.Results => Strings.Get("Wiz_Head_Results"),
        _ => string.Empty,
    };

    /// <summary>Verzija u bocnoj traci; ista ona koja ide u izvestaj.</summary>
    public string AppVersionLine => Strings.Format("Wiz_Version", AppVersion);

    private static string AppVersion =>
        typeof(MainViewModel).Assembly.GetName().Version?.ToString(3) ?? "?";

    public string StepSubtitle => Step switch
    {
        WizardStep.Welcome => Strings.Get("Wiz_Sub_Welcome"),
        WizardStep.Running => Strings.Get("Wiz_Sub_Running"),
        WizardStep.Questions => Strings.Get("Wiz_Sub_Questions"),
        WizardStep.Results => Strings.Get("Wiz_Sub_Results"),
        _ => string.Empty,
    };

    // --- podesavanja ---------------------------------------------------------

    private bool _useSimulator;
    /// <summary>
    /// Proba bez skenera. Postoji da bi se paket mogao isprobati pre slanja -
    /// to je acceptance gate faze G2710-11.
    /// </summary>
    public bool UseSimulator
    {
        get => _useSimulator;
        set => Set(ref _useSimulator, value);
    }

    private int _safetyLevel = 5;
    public int SafetyLevel
    {
        get => _safetyLevel;
        set => Set(ref _safetyLevel, Math.Clamp(value, 1, 5));
    }

    // --- rezultat ------------------------------------------------------------

    private QualificationReport? _report;
    public QualificationReport? Report
    {
        get => _report;
        private set
        {
            if (Set(ref _report, value))
            {
                Raise(nameof(DeviceLabel));
                Raise(nameof(SummaryLine));
                Raise(nameof(HasFailures));
                RefreshCommands();
            }
        }
    }

    public ObservableCollection<CheckRowViewModel> Checks { get; } = new();
    public ObservableCollection<CheckRowViewModel> Questions { get; } = new();
    public ObservableCollection<string> Log { get; } = new();

    private double _progress;
    public double Progress
    {
        get => _progress;
        private set => Set(ref _progress, value);
    }

    private string _statusLine = string.Empty;
    public string StatusLine
    {
        get => _statusLine;
        private set => Set(ref _statusLine, value);
    }

    private string _errorTitle = string.Empty;
    public string ErrorTitle
    {
        get => _errorTitle;
        private set => Set(ref _errorTitle, value);
    }

    private string _errorDetail = string.Empty;
    public string ErrorDetail
    {
        get => _errorDetail;
        private set => Set(ref _errorDetail, value);
    }

    private string? _savedReportPath;
    public string? SavedReportPath
    {
        get => _savedReportPath;
        private set
        {
            if (Set(ref _savedReportPath, value))
            {
                Raise(nameof(DeliverablePath));
                RefreshCommands();
            }
        }
    }

    public string DeviceLabel =>
        Report is null
            ? Strings.Get("Wiz_Results_NotChecked")
            : Strings.Format("Wiz_Results_Device", Report.Device);

    public string SummaryLine
    {
        get
        {
            if (Report is null)
            {
                return string.Empty;
            }
            var failures = Report.FailureCount();
            var passed = Report.Tests.Count(t => t.Outcome == CheckOutcome.Pass);
            return failures == 0
                ? Strings.Format("Wiz_Results_AllPassed", passed)
                : Strings.Format("Wiz_Results_SomeFailed", failures, Report.Tests.Count);
        }
    }

    public bool HasFailures => Report is not null && Report.FailureCount() > 0;

    // --- komande -------------------------------------------------------------

    public RelayCommand StartCommand { get; }
    public RelayCommand CancelCommand { get; }
    public RelayCommand RetryCommand { get; }
    public RelayCommand NextCommand { get; }
    public RelayCommand BackCommand { get; }
    public RelayCommand SaveReportCommand { get; }
    public RelayCommand OpenReportFolderCommand { get; }

    private void RefreshCommands()
    {
        StartCommand.RaiseCanExecuteChanged();
        CancelCommand.RaiseCanExecuteChanged();
        RetryCommand.RaiseCanExecuteChanged();
        NextCommand.RaiseCanExecuteChanged();
        BackCommand.RaiseCanExecuteChanged();
        SaveReportCommand.RaiseCanExecuteChanged();
        OpenReportFolderCommand.RaiseCanExecuteChanged();
    }

    public async Task StartAsync()
    {
        Step = WizardStep.Running;
        State = ScreenState.Busy;
        Progress = 0;
        StatusLine = Strings.Get("Wiz_Status_Locating");
        ErrorTitle = string.Empty;
        ErrorDetail = string.Empty;
        Log.Clear();
        Checks.Clear();
        Questions.Clear();
        Report = null;
        SavedReportPath = null;
        PackagePath = null;

        var located = _locate();
        if (!located.Found)
        {
            Fail(Strings.Get("Wiz_Fail_NoTool"),
                 Strings.Get("Wiz_Fail_NoTool_Detail") + "\n\n  "
                 + string.Join("\n  ", located.SearchedIn));
            return;
        }

        StatusLine = Strings.Get("Wiz_Status_Checking");
        _cancellation = new CancellationTokenSource();

        RunOutcome outcome;
        try
        {
            var runner = _runnerFactory(located.Path!);
            outcome = await runner
                .RunAsync(UseSimulator ? "sim" : "usbscan", SafetyLevel, AppendLog,
                          _cancellation.Token)
                .ConfigureAwait(true);
        }
        finally
        {
            _cancellation?.Dispose();
            _cancellation = null;
        }

        if (outcome.Failure != RunFailure.None || outcome.Report is null)
        {
            FailFromOutcome(outcome);
            return;
        }

        Report = outcome.Report;
        Progress = 100;

        foreach (var check in outcome.Report.Tests)
        {
            var row = new CheckRowViewModel(check);
            row.AnswerChanged = RefreshCommands;
            Checks.Add(row);

            if (row.NeedsAnswer)
            {
                Questions.Add(row);
            }
        }

        if (Checks.Count == 0)
        {
            State = ScreenState.Empty;
            StatusLine = Strings.Get("Wiz_Status_NoChecks");
            return;
        }

        State = ScreenState.Ready;
        StatusLine = Strings.Format("Wiz_Status_Done", Checks.Count);

        // Ako nema pitanja, korak sa pitanjima se preskace - prazan ekran sa
        // "nema nista" bio bi samo jedan klik vise.
        Step = Questions.Count > 0 ? WizardStep.Questions : WizardStep.Results;
    }

    public void Cancel()
    {
        _cancellation?.Cancel();
        StatusLine = Strings.Get("Wiz_Status_Stopping");
    }

    private void AppendLog(string line)
    {
        if (string.IsNullOrWhiteSpace(line))
        {
            return;
        }

        OnUiThread(() =>
        {
            // Ispis alata je vec grupisan po odeljcima; wizard ga prikazuje
            // kakav jeste, jer prijatelj taj tekst salje nazad kada nesto zapne.
            Log.Add(line);

            // Napredak se izvodi iz broja redova rezultata koji su prosli. Alat
            // ne javlja procenat, a lazna traka koja stoji na 50% je gora od
            // nikakve.
            if (line.TrimStart().StartsWith('['))
            {
                Progress = Math.Min(95, Progress + 4);
            }
        });
    }

    private void Fail(string title, string detail)
    {
        ErrorTitle = title;
        ErrorDetail = detail;
        State = ScreenState.Failed;
        StatusLine = title;
    }

    private void FailFromOutcome(RunOutcome outcome)
    {
        var (title, detail) = outcome.Failure switch
        {
            RunFailure.ToolMissing => (
                Strings.Get("Wiz_Fail_NoTool"),
                Strings.Get("Wiz_Fail_NoTool_Short")),

            RunFailure.Cancelled => (
                Strings.Get("Wiz_Fail_Cancelled"),
                Strings.Get("Wiz_Fail_Cancelled_Detail")),

            RunFailure.DeviceNotFound => (
                Strings.Get("Wiz_Fail_NotFound"),
                Strings.Get("Wiz_Fail_NotFound_Detail")),

            RunFailure.DeviceBusy => (
                Strings.Get("Wiz_Fail_Busy"),
                Strings.Get("Wiz_Fail_Busy_Detail")),

            RunFailure.ToolCrashed => (
                Strings.Get("Wiz_Fail_NoStart"),
                Strings.Get("Wiz_Fail_NoStart_Detail")),

            _ => (
                Strings.Get("Wiz_Fail_NoReport"),
                Strings.Get("Wiz_Fail_NoReport_Detail")),
        };

        var full = detail;
        if (!string.IsNullOrWhiteSpace(outcome.ToolOutput))
        {
            full += "\n\n" + Strings.Get("Wiz_Fail_ToolSaid") + "\n" + outcome.ToolOutput.Trim();
        }
        Fail(title, full);
    }

    // --- kretanje kroz korake ------------------------------------------------

    private bool CanGoNext() => Step switch
    {
        WizardStep.Welcome => State != ScreenState.Busy,
        WizardStep.Questions => Questions.All(q => q.IsAnswered),
        _ => false,
    };

    private void GoNext()
    {
        switch (Step)
        {
            case WizardStep.Welcome:
                _ = StartAsync();
                break;
            case WizardStep.Questions:
                Step = WizardStep.Results;
                break;
        }
    }

    private bool CanGoBack() =>
        State != ScreenState.Busy && Step is WizardStep.Questions or WizardStep.Results;

    private void GoBack()
    {
        Step = Step switch
        {
            WizardStep.Results when Questions.Count > 0 => WizardStep.Questions,
            WizardStep.Results => WizardStep.Welcome,
            WizardStep.Questions => WizardStep.Welcome,
            _ => Step,
        };
    }

    // --- izvestaj ------------------------------------------------------------

    /// <summary>Gde se izvestaj cuva. Izdvojeno da se moze testirati.</summary>
    public Func<string> ReportDirectory { get; set; } =
        () => Environment.GetFolderPath(Environment.SpecialFolder.DesktopDirectory);

    public void SaveReport()
    {
        if (Report is null)
        {
            return;
        }

        try
        {
            var stamp = DateTime.Now.ToString("yyyyMMdd-HHmmss");
            var name = $"G2710-HardwareReport-{stamp}.json";
            var path = Path.Combine(ReportDirectory(), name);

            File.WriteAllText(path, Report.ToJson());
            SavedReportPath = path;
            StatusLine = Strings.Get("Wiz_Status_Saved");
        }
        catch (Exception exception) when (exception is IOException
                                              or UnauthorizedAccessException)
        {
            Fail(Strings.Get("Wiz_Fail_NotSaved"), exception.Message);
        }
    }

    // --- pakovanje -----------------------------------------------------------

    /// <summary>
    /// Sakuplja podatke o masini i pakuje sve u jedan ZIP. Izdvojeno da se
    /// moze zameniti u testovima - pravi posao radi collect-diagnostics.ps1.
    /// </summary>
    public Func<string, string, DiagnosticsPackager.Result> PackageDiagnostics { get; set; } =
        (reportPath, directory) =>
            DiagnosticsPackager.Run(reportPath, directory, TimeSpan.FromMinutes(2));

    /// <summary>Putanja do ZIP-a koji se salje nazad, kada nastane.</summary>
    private string? _packagePath;
    public string? PackagePath
    {
        get => _packagePath;
        private set
        {
            if (Set(ref _packagePath, value))
            {
                Raise(nameof(DeliverablePath));
            }
        }
    }

    /// <summary>
    /// Sacuvaj izvestaj, pa ga upakuj zajedno sa podacima o racunaru.
    /// </summary>
    /// <remarks>
    /// Pakovanje sme da padne, i to nista ne rusi: izvestaj je vec na disku i
    /// wizard i dalje pokazuje njegovu putanju. Zato greska pakovanja ide u
    /// StatusLine, a ne kroz Fail() - Fail() vodi na ekran greske, a ovde se
    /// nije desilo nista sto bi zasluzilo da sakrije uspesno sacuvan izvestaj.
    /// </remarks>
    public async Task SaveAndPackageAsync()
    {
        SaveReport();
        if (SavedReportPath is null)
        {
            return;
        }

        var reportPath = SavedReportPath;
        var directory = ReportDirectory();

        State = ScreenState.Busy;
        StatusLine = Strings.Get("Wiz_Status_Collecting");

        // Skript cita registar, pnputil i setupapi log - traje par sekundi.
        // Na UI niti bi to bio zamrznut prozor.
        var result = await Task.Run(() => PackageDiagnostics(reportPath, directory))
                               .ConfigureAwait(true);

        State = ScreenState.Ready;

        if (result.Ok)
        {
            PackagePath = result.ZipPath;

            // Izvestaj je sada UNUTAR ZIP-a, pa goli JSON pored njega samo
            // zbunjuje: uputstvo kaze "posaljite taj jedan fajl", a na radnoj
            // povrsini stoje dva i oba se zovu isto.
            //
            // Brise se tek posle uspesnog pakovanja, i nikad ako pakovanje
            // padne - tada je taj JSON jedino sto covek ima.
            TryDelete(reportPath);
            SavedReportPath = null;

            StatusLine = Strings.Get("Wiz_Status_Ready");
        }
        else
        {
            StatusLine = Strings.Format("Wiz_Status_NoZip", result.Error);
        }
    }

    /// <summary>
    /// Sta se salje nazad: ZIP ako je nastao, inace goli izvestaj. Prikaz i
    /// dugme "otvori folder" gadjaju ovo, ne dva razlicita polja.
    /// </summary>
    public string? DeliverablePath => PackagePath ?? SavedReportPath;

    /// <summary>Brisanje koje ne sme da obori ono sto je vec uspelo.</summary>
    public Action<string> TryDelete { get; set; } = path =>
    {
        try
        {
            File.Delete(path);
        }
        catch (Exception exception) when (exception is IOException
                                              or UnauthorizedAccessException
                                              or NotSupportedException)
        {
            // Fajl je zakljucan ili ga vise nema. Kopija je u ZIP-u, pa ovo
            // nije vest za coveka koji testira skener.
        }
    };

    private void OpenReportFolder()
    {
        var target = DeliverablePath;
        if (target is null)
        {
            return;
        }

        try
        {
            System.Diagnostics.Process.Start(new System.Diagnostics.ProcessStartInfo
            {
                FileName = "explorer.exe",
                Arguments = $"/select,\"{target}\"",
                UseShellExecute = true,
            });
        }
        catch (Exception exception) when (exception is System.ComponentModel.Win32Exception
                                              or InvalidOperationException)
        {
            StatusLine = Strings.Get("Wiz_Status_NoFolder");
        }
    }
}
