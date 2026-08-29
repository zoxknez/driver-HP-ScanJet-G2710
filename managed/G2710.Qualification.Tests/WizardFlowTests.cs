using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;
using G2710.Qualification.Models;
using G2710.Qualification.Services;
using G2710.Qualification.ViewModels;
using Xunit;

namespace G2710.Qualification.Tests;

/// <summary>
/// Ceo tok wizarda, bez otvaranja prozora.
/// </summary>
/// <remarks>
/// Moguce je zato sto sve odluke stoje u MainViewModel-u, a pogledi samo vezuju
/// vrednosti. Da su odluke u code-behind-u, ovi testovi ne bi postojali.
/// </remarks>
public class WizardFlowTests
{
    private static QualificationReport ReportWith(params (string Id, string Result)[] tests)
    {
        var report = new QualificationReport { Device = "03F0-2805", Timestamp = "T" };
        foreach (var (id, result) in tests)
        {
            report.Tests.Add(new CheckResult
            {
                Id = id,
                Name = "provera " + id,
                ResultWord = result,
                Question = result == "ASK" ? "Da li nesto?" : string.Empty,
                Detail = result == "ASK" ? string.Empty : "detalj",
            });
        }
        return report;
    }

    private static MainViewModel BuildViewModel(RunOutcome outcome,
                                                IReadOnlyList<string>? lines = null,
                                                bool toolFound = true)
    {
        var located = toolFound
            ? new ToolLocator.Result("C:/lazni/g2710ctl.exe", new[] { "C:/lazni/g2710ctl.exe" })
            : new ToolLocator.Result(null, new[] { "C:/prvo", "C:/drugo" });

        return new MainViewModel(
            locate: () => located,
            runnerFactory: _ => new StubRunner(outcome, lines));
    }

    /// <summary>Runner koji ne pokrece nijedan proces.</summary>
    private sealed class StubRunner : QualificationRunner
    {
        private readonly RunOutcome _outcome;
        private readonly IReadOnlyList<string> _lines;

        public StubRunner(RunOutcome outcome, IReadOnlyList<string>? lines)
            : base(Path.Combine(Path.GetTempPath(), "g2710ctl.exe"))
        {
            _outcome = outcome;
            _lines = lines ?? Array.Empty<string>();
        }

        public override async Task<RunOutcome> RunAsync(string transport,
                                                        int safetyLevel,
                                                        Action<string>? onOutput,
                                                        CancellationToken token)
        {
            foreach (var line in _lines)
            {
                onOutput?.Invoke(line);
            }
            await Task.Yield();
            return _outcome;
        }
    }

    // --- pocetno stanje ---------------------------------------------------------

    [Fact]
    public void StartsOnTheWelcomeStep()
    {
        var model = new MainViewModel(() => new ToolLocator.Result(null, Array.Empty<string>()));

        Assert.Equal(WizardStep.Welcome, model.Step);
        Assert.True(model.IsWelcome);
        Assert.Equal(ScreenState.Idle, model.State);
        Assert.Empty(model.Checks);
        Assert.Null(model.Report);
    }

    [Fact]
    public void EveryStepHasATitleAndSubtitle()
    {
        var model = new MainViewModel(() => new ToolLocator.Result(null, Array.Empty<string>()));

        foreach (WizardStep step in Enum.GetValues<WizardStep>())
        {
            typeof(MainViewModel).GetProperty(nameof(MainViewModel.Step))!
                .SetValue(model, step);

            Assert.False(string.IsNullOrWhiteSpace(model.StepTitle), step.ToString());
            Assert.False(string.IsNullOrWhiteSpace(model.StepSubtitle), step.ToString());
        }
    }

    [Fact]
    public void SafetyLevelIsClampedToTheValidRange()
    {
        var model = new MainViewModel(() => new ToolLocator.Result(null, Array.Empty<string>()));

        model.SafetyLevel = 99;
        Assert.Equal(5, model.SafetyLevel);

        model.SafetyLevel = -3;
        Assert.Equal(1, model.SafetyLevel);
    }

    // Korak je zavrsen kada je tekuci iza njega. Vezivanje za "jesmo li na
    // sledecem koraku" izgleda isto dok se ide redom, a pogresi cim se jedan
    // preskoci - i zavrseni korak se vrati u broj.
    [Fact]
    public async Task FinishedStepsStayFinishedEvenWhenOneIsSkipped()
    {
        var report = ReportWith(("H1.1", "PASS"));   // bez pitanja - korak 3 se preskace
        var model = BuildViewModel(new RunOutcome(RunFailure.None, report, string.Empty, 0));

        await model.StartAsync();
        Assert.Equal(WizardStep.Results, model.Step);

        Assert.True(model.Step1Done);
        Assert.True(model.Step2Done);
        Assert.True(model.Step3Done);
    }

    [Fact]
    public void NoStepIsFinishedAtTheStart()
    {
        var model = new MainViewModel(() => new ToolLocator.Result(null, Array.Empty<string>()));

        Assert.False(model.Step1Done);
        Assert.False(model.Step2Done);
        Assert.False(model.Step3Done);
    }

    // --- alat nije pronadjen -----------------------------------------------------

    // Poruka mora reci GDE je trazeno. "Nije pronadjen" ne pomaze nikome tko
    // ne zna gde bi fajl trebalo da stoji.
    [Fact]
    public async Task MissingToolExplainsWhereItLooked()
    {
        var model = new MainViewModel(
            () => new ToolLocator.Result(null, new[] { "C:/prvo/g2710ctl.exe",
                                                       "C:/drugo/g2710ctl.exe" }));

        await model.StartAsync();

        Assert.Equal(ScreenState.Failed, model.State);
        Assert.Contains("not found", model.ErrorTitle, StringComparison.OrdinalIgnoreCase);
        Assert.Contains("C:/prvo", model.ErrorDetail);
        Assert.Contains("C:/drugo", model.ErrorDetail);
    }

    // --- pitanja ------------------------------------------------------------------

    [Fact]
    public async Task StopsOnTheQuestionStepWhenThereAreQuestions()
    {
        var report = ReportWith(("H1.1", "PASS"), ("H3.3", "ASK"));
        var model = BuildViewModel(new RunOutcome(RunFailure.None, report, string.Empty, 0));

        await model.StartAsync();

        Assert.Equal(WizardStep.Questions, model.Step);
        Assert.Single(model.Questions);
        Assert.Equal(2, model.Checks.Count);
    }

    // Bez pitanja se korak preskace: prazan ekran sa "nema nista" bio bi samo
    // jedan klik vise.
    [Fact]
    public async Task SkipsTheQuestionStepWhenThereAreNone()
    {
        var report = ReportWith(("H1.1", "PASS"), ("H2.1", "PASS"));
        var model = BuildViewModel(new RunOutcome(RunFailure.None, report, string.Empty, 0));

        await model.StartAsync();

        Assert.Equal(WizardStep.Results, model.Step);
        Assert.Empty(model.Questions);
    }

    [Fact]
    public async Task CannotMoveOnUntilEveryQuestionIsAnswered()
    {
        var report = ReportWith(("H3.3", "ASK"), ("H10.1", "ASK"));
        var model = BuildViewModel(new RunOutcome(RunFailure.None, report, string.Empty, 0));

        await model.StartAsync();
        Assert.False(model.NextCommand.CanExecute(null));

        model.Questions[0].Answer = UserAnswer.Yes;
        Assert.False(model.NextCommand.CanExecute(null));

        model.Questions[1].Answer = UserAnswer.No;
        Assert.True(model.NextCommand.CanExecute(null));
    }

    [Fact]
    public async Task AnsweringNoTurnsIntoAFailure()
    {
        var report = ReportWith(("H1.1", "PASS"), ("H3.3", "ASK"));
        var model = BuildViewModel(new RunOutcome(RunFailure.None, report, string.Empty, 0));

        await model.StartAsync();
        Assert.False(model.HasFailures);

        model.Questions[0].Answer = UserAnswer.No;
        Assert.Equal(1, model.Report!.FailureCount());
    }

    // --- neuspesni prolaz ----------------------------------------------------------

    // Oba jezika: prevod koji izgubi radnju ("proverite kabl") pretvorio bi
    // uputstvo u obavestenje, i to bi se videlo tek kod prijatelja.
    [Theory]
    [InlineData("en", RunFailure.DeviceNotFound, "cable")]
    [InlineData("en", RunFailure.DeviceBusy, "program")]
    [InlineData("en", RunFailure.ToolCrashed, "start")]
    [InlineData("en", RunFailure.Cancelled, "stopped")]
    [InlineData("en", RunFailure.NoReport, "report")]
    [InlineData("sr", RunFailure.DeviceNotFound, "kabl")]
    [InlineData("sr", RunFailure.DeviceBusy, "program")]
    [InlineData("sr", RunFailure.ToolCrashed, "pokrenuo")]
    [InlineData("sr", RunFailure.Cancelled, "prekinuta")]
    [InlineData("sr", RunFailure.NoReport, "izveštaj")]
    public async Task EveryFailureHasAMessageAUserCanAct(string language,
                                                          RunFailure failure,
                                                          string expected)
    {
        using var _ = new LanguageScope(language);

        var model = BuildViewModel(new RunOutcome(failure, null, string.Empty, 3));

        await model.StartAsync();

        Assert.Equal(ScreenState.Failed, model.State);
        Assert.False(string.IsNullOrWhiteSpace(model.ErrorTitle));
        Assert.False(string.IsNullOrWhiteSpace(model.ErrorDetail));

        // Korisnik cita CELU karticu, pa se i trazi u celoj - nekad je kljucna
        // rec u naslovu, nekad u objasnjenju.
        var message = model.ErrorTitle + " " + model.ErrorDetail;
        Assert.Contains(expected, message, StringComparison.OrdinalIgnoreCase);

        Assert.True(model.RetryCommand.CanExecute(null));
    }

    [Fact]
    public async Task FailureCarriesWhatTheToolPrinted()
    {
        var model = BuildViewModel(
            new RunOutcome(RunFailure.NoReport, null, "nesto je alat rekao", 15));

        await model.StartAsync();

        Assert.Contains("nesto je alat rekao", model.ErrorDetail);
    }

    // Izvestaj bez ijedne provere NIJE greska nego prazan ishod, i ima svoj
    // prikaz. Bez toga bi korisnik gledao prazan ekran bez objasnjenja.
    [Fact]
    public async Task EmptyReportIsItsOwnStateNotAFailure()
    {
        var report = new QualificationReport { Device = "X" };
        report.Tests.Add(new CheckResult { Id = "H1.1", ResultWord = "PASS" });
        report.Tests.Clear();

        var model = BuildViewModel(new RunOutcome(RunFailure.None, report, string.Empty, 0));
        await model.StartAsync();

        Assert.Equal(ScreenState.Empty, model.State);
        Assert.False(model.IsFailed);
    }

    // --- izlazni kodovi -------------------------------------------------------------

    [Theory]
    [InlineData(3, RunFailure.DeviceNotFound)]
    [InlineData(6, RunFailure.DeviceNotFound)]
    [InlineData(7, RunFailure.DeviceBusy)]
    [InlineData(15, RunFailure.NoReport)]
    [InlineData(0, RunFailure.NoReport)]
    public void ExitCodesMapToReasons(int exitCode, RunFailure expected)
    {
        Assert.Equal(expected, QualificationRunner.ClassifyExit(exitCode));
    }

    // --- kretanje unazad --------------------------------------------------------------

    [Fact]
    public async Task BackFromResultsReturnsToQuestionsWhenThereAreSome()
    {
        var report = ReportWith(("H3.3", "ASK"));
        var model = BuildViewModel(new RunOutcome(RunFailure.None, report, string.Empty, 0));

        await model.StartAsync();
        model.Questions[0].Answer = UserAnswer.Yes;
        model.NextCommand.Execute(null);
        Assert.Equal(WizardStep.Results, model.Step);

        model.BackCommand.Execute(null);
        Assert.Equal(WizardStep.Questions, model.Step);
    }

    [Fact]
    public async Task BackFromResultsReturnsToWelcomeWhenThereAreNoQuestions()
    {
        var report = ReportWith(("H1.1", "PASS"));
        var model = BuildViewModel(new RunOutcome(RunFailure.None, report, string.Empty, 0));

        await model.StartAsync();
        Assert.Equal(WizardStep.Results, model.Step);

        model.BackCommand.Execute(null);
        Assert.Equal(WizardStep.Welcome, model.Step);
    }

    // --- cuvanje izvestaja --------------------------------------------------------------

    [Fact]
    public async Task SavingWritesAReportThatCanBeReadBack()
    {
        var directory = Path.Combine(Path.GetTempPath(),
                                     "g2710-wizard-" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(directory);

        try
        {
            var report = ReportWith(("H1.1", "PASS"), ("H3.3", "ASK"));
            var model = BuildViewModel(new RunOutcome(RunFailure.None, report, string.Empty, 0));
            model.ReportDirectory = () => directory;

            await model.StartAsync();
            model.Questions[0].Answer = UserAnswer.No;
            model.SaveReport();

            Assert.NotNull(model.SavedReportPath);
            Assert.True(File.Exists(model.SavedReportPath));

            var saved = QualificationReport.TryParse(File.ReadAllText(model.SavedReportPath!));
            Assert.NotNull(saved);
            Assert.Equal(UserAnswer.No, saved!.Questions().First().Answer);
        }
        finally
        {
            Directory.Delete(directory, recursive: true);
        }
    }

    [Fact]
    public void SavingWithoutAReportDoesNothing()
    {
        var model = new MainViewModel(() => new ToolLocator.Result(null, Array.Empty<string>()));

        Assert.False(model.SaveReportCommand.CanExecute(null));
        model.SaveReport();
        Assert.Null(model.SavedReportPath);
    }

    [Fact]
    public async Task SavingIntoAnUnwritablePlaceIsReportedNotThrown()
    {
        var report = ReportWith(("H1.1", "PASS"));
        var model = BuildViewModel(new RunOutcome(RunFailure.None, report, string.Empty, 0));
        model.ReportDirectory = () => "Z:/ovo/ne/postoji";

        await model.StartAsync();
        model.SaveReport();

        Assert.Equal(ScreenState.Failed, model.State);
        Assert.Contains("not saved", model.ErrorTitle, StringComparison.OrdinalIgnoreCase);
    }

    // --- pakovanje u ZIP -----------------------------------------------------------------

    [Fact]
    public async Task PackagingTurnsTheSavedReportIntoTheZipThatGetsSent()
    {
        var directory = Path.Combine(Path.GetTempPath(),
                                     "g2710-wizard-" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(directory);

        try
        {
            var report = ReportWith(("H1.1", "PASS"));
            var model = BuildViewModel(new RunOutcome(RunFailure.None, report, string.Empty, 0));
            model.ReportDirectory = () => directory;

            string? seenReport = null;
            var deleted = new List<string>();
            var zip = Path.Combine(directory, "G2710-HardwareReport-x.zip");
            model.PackageDiagnostics = (reportPath, dir) =>
            {
                seenReport = reportPath;
                return new DiagnosticsPackager.Result(zip, null);
            };
            model.TryDelete = deleted.Add;

            await model.StartAsync();
            await model.SaveAndPackageAsync();

            // Skript mora dobiti TACAN fajl koji je wizard upravo napisao.
            // Bez toga bi pokupio najnoviji na radnoj povrsini - sto je tudji
            // izvestaj od proslog puta.
            Assert.NotNull(seenReport);
            Assert.Equal(directory, Path.GetDirectoryName(seenReport));
            Assert.Equal(".json", Path.GetExtension(seenReport));

            Assert.Equal(zip, model.PackagePath);
            Assert.Equal(zip, model.DeliverablePath);
            Assert.Equal(ScreenState.Ready, model.State);

            // Goli izvestaj se brise kada je vec u ZIP-u: uputstvo trazi da se
            // posalje JEDAN fajl, pa na radnoj povrsini sme da stoji jedan.
            Assert.Equal(new[] { seenReport }, deleted);
            Assert.Null(model.SavedReportPath);
        }
        finally
        {
            Directory.Delete(directory, recursive: true);
        }
    }

    // Pakovanje sme da padne. Izvestaj je vec na disku i covek mora da vidi
    // gde je - inace mu wizard oduzme i ono sto je uspelo.
    [Fact]
    public async Task AFailedPackagingStillLeavesTheReportInHand()
    {
        var directory = Path.Combine(Path.GetTempPath(),
                                     "g2710-wizard-" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(directory);

        try
        {
            var report = ReportWith(("H1.1", "PASS"));
            var model = BuildViewModel(new RunOutcome(RunFailure.None, report, string.Empty, 0));
            model.ReportDirectory = () => directory;
            model.PackageDiagnostics =
                (_, _) => new DiagnosticsPackager.Result(null, "PowerShell nije nadjen.");

            var deleted = new List<string>();
            model.TryDelete = deleted.Add;

            await model.StartAsync();
            await model.SaveAndPackageAsync();

            Assert.Empty(deleted);
            Assert.Null(model.PackagePath);
            Assert.NotNull(model.SavedReportPath);
            Assert.True(File.Exists(model.SavedReportPath));

            // Ne ekran greske - izvestaj je sacuvan, samo ZIP nije nastao.
            Assert.NotEqual(ScreenState.Failed, model.State);
            Assert.Contains("saved", model.StatusLine, StringComparison.OrdinalIgnoreCase);
            Assert.Contains("PowerShell nije nadjen", model.StatusLine,
                            StringComparison.OrdinalIgnoreCase);

            // Dugme "otvori folder" i dalje ima sta da pokaze.
            Assert.Equal(model.SavedReportPath, model.DeliverablePath);
            Assert.True(model.OpenReportFolderCommand.CanExecute(null));
        }
        finally
        {
            Directory.Delete(directory, recursive: true);
        }
    }

    // Ako snimanje padne, pakovanje se NE pokrece. Inace bi se skriptu
    // prosledila putanja fajla koji ne postoji.
    [Fact]
    public async Task PackagingIsSkippedWhenTheReportCouldNotBeSaved()
    {
        var report = ReportWith(("H1.1", "PASS"));
        var model = BuildViewModel(new RunOutcome(RunFailure.None, report, string.Empty, 0));
        model.ReportDirectory = () => "Z:/ovo/ne/postoji";

        var called = false;
        model.PackageDiagnostics = (_, _) =>
        {
            called = true;
            return new DiagnosticsPackager.Result(null, null);
        };

        await model.StartAsync();
        await model.SaveAndPackageAsync();

        Assert.False(called);
        Assert.Equal(ScreenState.Failed, model.State);
    }

    // Nova provera brise stari ZIP iz prikaza. Da ostane, covek bi poslao
    // izvestaj od proslog puta.
    [Fact]
    public async Task StartingOverClearsThePreviousPackage()
    {
        var directory = Path.Combine(Path.GetTempPath(),
                                     "g2710-wizard-" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(directory);

        try
        {
            var report = ReportWith(("H1.1", "PASS"));
            var model = BuildViewModel(new RunOutcome(RunFailure.None, report, string.Empty, 0));
            model.ReportDirectory = () => directory;
            model.PackageDiagnostics =
                (_, _) => new DiagnosticsPackager.Result(
                    Path.Combine(directory, "G2710-HardwareReport-x.zip"), null);

            await model.StartAsync();
            await model.SaveAndPackageAsync();
            Assert.NotNull(model.PackagePath);

            await model.StartAsync();

            Assert.Null(model.PackagePath);
            Assert.Null(model.SavedReportPath);
            Assert.Null(model.DeliverablePath);
        }
        finally
        {
            Directory.Delete(directory, recursive: true);
        }
    }

    // --- sam pakovac, bez wizarda --------------------------------------------------------

    [Fact]
    public void ThePackagerSaysWhichScriptIsMissingInsteadOfFailingSilently()
    {
        // U test okruzenju collect-diagnostics.ps1 ne stoji pored binarnog
        // fajla, pa ovo pokriva bas onu granu koja se u paketu ne desava.
        var result = DiagnosticsPackager.Run("C:/nema.json", Path.GetTempPath(),
                                             TimeSpan.FromSeconds(5));

        Assert.False(result.Ok);
        Assert.NotNull(result.Error);
        Assert.Contains(DiagnosticsPackager.ScriptName, result.Error!,
                        StringComparison.OrdinalIgnoreCase);
    }
}
