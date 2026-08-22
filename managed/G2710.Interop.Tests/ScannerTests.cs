using System.Collections.Concurrent;
using G2710.Interop;
using Xunit;

namespace G2710.Interop.Tests;

/// <summary>
/// Pozivi kroz PRAVU G2710.Native.dll, nad simulatorom.
/// </summary>
/// <remarks>
/// Bez laznjaka, i to nije stvar temeljnosti nego nuzde: ono sto se ovde meri -
/// raspored struktura, zivotni vek callback-ova, SafeHandle - ne postoji ni nad
/// cim drugim. Laznjak bi merio laznjak.
/// </remarks>
public class ScannerTests
{
    private static ScannerOptions Simulated(int level = 5, bool trace = false) => new()
    {
        Transport = ScannerTransport.Simulator,
        RequestedSafetyLevel = level,
        ClientName = "interop-test",
        RecordTrace = trace,
    };

    private static ScanSettings SmallScan() => new()
    {
        Resolution = 300,
        ColorMode = ScanColorMode.Color,
        BitsPerChannel = 8,
        Width = 64,
        Height = 8,
        AllowUnqualified = true,
    };

    private static Scanner OpenReady(int level = 5, bool trace = false)
    {
        Scanner scanner = Scanner.Open(Simulated(level, trace));
        scanner.Identify();
        scanner.Begin();
        return scanner;
    }

    // =========================================================================
    // Ugovor
    // =========================================================================

    [Fact]
    public void TheLibraryMatchesTheContractThisSideWasBuiltFor()
    {
        // Ako ovo padne, nijedan test ispod ne znaci nista - i nijedan poziv u
        // aplikaciji ne bi bio bezbedan.
        Scanner.CheckAbiVersion();
        Assert.Equal(Scanner.ExpectedAbiVersion, Scanner.NativeAbiVersion);
    }

    [Fact]
    public void TheBuildCeilingIsReadableWithoutOpeningAnything()
    {
        int ceiling = Scanner.BuildSafetyCeiling;
        Assert.InRange(ceiling, 1, 5);

        // Ispod nivoa 3 motorni put nije ni preveden. Aplikacija to mora moci
        // reci coveku pre nego sto ponudi dugme koje pomera glavu.
        if (ceiling < 3)
        {
            Assert.False(Scanner.MotorPathCompiled);
        }
    }

    [Fact]
    public void AMismatchedAbiIsItsOwnKindOfFailure()
    {
        // Poseban tip zato sto je i lek poseban: nijedan drugi poziv nece
        // proci, pa nema smisla pokusavati.
        var exception = new AbiMismatchException(0x00010000, 0x00020003);
        Assert.Contains("2.3", exception.Message);
        Assert.Contains("1.0", exception.Message);
    }

    // =========================================================================
    // Otvaranje i zatvaranje
    // =========================================================================

    [Fact]
    public void OpeningTheSimulatorSucceedsAndReportsIdleAfterBegin()
    {
        using Scanner scanner = OpenReady();
        Assert.Equal(ScannerState.Idle, scanner.State);
        Assert.Equal(Math.Min(Scanner.BuildSafetyCeiling, 5), scanner.EffectiveSafetyLevel);
    }

    [Fact]
    public void ASafetyLevelOutsideOneToFiveIsRefusedBeforeAnyNativeCall()
    {
        Assert.Throws<ArgumentOutOfRangeException>(() =>
            Scanner.Open(new ScannerOptions { RequestedSafetyLevel = 0 }));
        Assert.Throws<ArgumentOutOfRangeException>(() =>
            Scanner.Open(new ScannerOptions { RequestedSafetyLevel = 6 }));
    }

    [Fact]
    public void RequestingLessThanTheCeilingStillGivesLess()
    {
        // Plafon spusta, ali ne DIZE.
        using Scanner scanner = OpenReady(level: 1);
        Assert.Equal(1, scanner.EffectiveSafetyLevel);
    }

    [Fact]
    public void ManyOpenAndCloseCyclesDoNotLeaveTheDeviceLocked()
    {
        // Uredjaj koji ostane otvoren drzi ekskluzivnu bravu. Da se ne
        // oslobadja, drugi krug bi pao sa Busy - a kod korisnika bi to bio
        // skener koji "koristi neko drugi" bez ijednog vidljivog programa.
        for (int i = 0; i < 20; ++i)
        {
            using Scanner scanner = OpenReady();
            Assert.Equal(ScannerState.Idle, scanner.State);
        }
    }

    [Fact]
    public void DisposingTwiceIsHarmless()
    {
        Scanner scanner = OpenReady();
        scanner.Dispose();
        scanner.Dispose();
    }

    [Fact]
    public void UsingAClosedScannerSaysSoInsteadOfCrashing()
    {
        Scanner scanner = OpenReady();
        scanner.Dispose();

        // Zatvoren handle proslednjen native strani je pristup oslobodjenoj
        // memoriji. ObjectDisposedException je jedina prihvatljiva zamena.
        Assert.Throws<ObjectDisposedException>(() => _ = scanner.State);
        Assert.Throws<ObjectDisposedException>(() => scanner.Identify());
        Assert.Throws<ObjectDisposedException>(() => scanner.ScanBegin(SmallScan()));
    }

    [Fact]
    public void CancelAfterDisposeIsIgnoredNotThrown()
    {
        // Dugme "Prekini" moze stici i posle zatvaranja prozora. Izuzetak bi
        // tada srusio aplikaciju pri gasenju.
        Scanner scanner = OpenReady();
        scanner.Dispose();
        scanner.Cancel();
    }

    // =========================================================================
    // Planiranje bez uredjaja
    // =========================================================================

    [Fact]
    public void PlanningNeedsNoDeviceAtAll()
    {
        ScanGeometry geometry = Scanner.Plan(SmallScan());

        Assert.Equal(64, geometry.WidthPixels);
        Assert.Equal(3, geometry.Channels);
        Assert.Equal(8, geometry.BitsPerChannel);
        Assert.Equal(64 * 3, geometry.BytesPerLine);
        Assert.Equal(300, geometry.NativeResolution);
    }

    [Fact]
    public void PlanningSaysWhereItWillReallyScan()
    {
        // 200 dpi nema svoj red u tabeli hardvera - skenira se na 300 pa
        // smanjuje. Aplikacija to mora moci da kaze korisniku.
        ScanGeometry geometry = Scanner.Plan(SmallScan() with { Resolution = 200 });
        Assert.Equal(300, geometry.NativeResolution);
    }

    [Fact]
    public void AnImpossibleDepthIsAnExceptionWithTheReasonInIt()
    {
        var exception = Assert.Throws<ScannerException>(() =>
            Scanner.Plan(SmallScan() with { BitsPerChannel = 12 }));

        Assert.Equal(ScanStatus.InvalidArgument, exception.Status);
    }

    // =========================================================================
    // Pun tok
    // =========================================================================

    [Fact]
    public void TheWholeFlowDeliversEveryLine()
    {
        using Scanner scanner = OpenReady();
        Assert.Equal(ScanStatus.Ok, scanner.Warmup(TimeSpan.FromMilliseconds(20)));

        ScanGeometry geometry = scanner.ScanBegin(SmallScan());
        Assert.True(geometry.Lines > 0);
        Assert.True(geometry.BytesPerLine > 0);

        var line = new byte[geometry.BytesPerLine];
        int delivered = 0;
        try
        {
            while (scanner.ScanReadLine(line) == ScanLineResult.Delivered)
            {
                ++delivered;
                Assert.True(delivered <= geometry.Lines * 2, "prolaz ne staje");
            }
        }
        finally
        {
            Assert.Equal(ScanStatus.Ok, scanner.ScanEnd());
        }

        Assert.Equal(geometry.Lines, delivered);
        scanner.End();
    }

    [Fact]
    public void ATooSmallLineBufferIsAnExceptionNotATruncatedImage()
    {
        using Scanner scanner = OpenReady();
        scanner.Warmup(TimeSpan.FromMilliseconds(20));
        ScanGeometry geometry = scanner.ScanBegin(SmallScan());

        try
        {
            var tooSmall = new byte[geometry.BytesPerLine - 1];
            var exception = Assert.Throws<ScannerException>(() => scanner.ScanReadLine(tooSmall));
            Assert.Equal(ScanStatus.InvalidArgument, exception.Status);

            // Poruka mora reci STA je pogresno; "invalid argument" sam za sebe
            // ne pomaze nikome.
            Assert.Contains("bytes_per_line", exception.Detail);
        }
        finally
        {
            scanner.ScanEnd();
        }
    }

    [Fact]
    public void EndingAScanThatNeverStartedIsNotAnError()
    {
        using Scanner scanner = OpenReady();

        // Pozivalac ovo radi u `finally` bloku i ne zna uvek da li je pocelo.
        Assert.Equal(ScanStatus.Ok, scanner.ScanEnd());
    }

    // =========================================================================
    // Otkazivanje
    // =========================================================================

    [Fact]
    public void CancelIsAValueNotAnException()
    {
        using Scanner scanner = OpenReady();
        scanner.Warmup(TimeSpan.FromMilliseconds(20));
        ScanGeometry geometry = scanner.ScanBegin(SmallScan() with { Height = 200 });

        var line = new byte[geometry.BytesPerLine];
        Assert.Equal(ScanLineResult.Delivered, scanner.ScanReadLine(line));

        scanner.Cancel();

        // Korisnik koji je pritisnuo "Prekini" nije naisao na gresku - dobio je
        // ono sto je trazio. Izuzetak bi terao svaki poziv u try/catch i sakrio
        // pravu gresku medju ocekivanim.
        //
        // Ali otkazano NIJE isto sto i gotovo: slika je nepotpuna, i to se mora
        // razlikovati. Prva verzija je vracala bool i ta razlika je nestajala.
        Assert.Equal(ScanLineResult.Cancelled, scanner.ScanReadLine(line));
        Assert.NotEqual(ScanLineResult.Complete, ScanLineResult.Cancelled);
        Assert.Equal(ScanStatus.Ok, scanner.ScanEnd());
    }

    [Fact]
    public async Task CancelFromAnotherThreadIsAllowed()
    {
        using Scanner scanner = OpenReady();
        scanner.Warmup(TimeSpan.FromMilliseconds(20));
        ScanGeometry geometry = scanner.ScanBegin(SmallScan() with { Height = 400 });

        // Rukovanje, ne trka: simulator isporuci sve redove brze nego sto bi
        // ijedno spavanje stiglo, pa bi test sa `await Task.Delay` merio srecu.
        var lineRead = new TaskCompletionSource();
        var cancelled = new TaskCompletionSource();

        Task stopper = Task.Run(async () =>
        {
            await lineRead.Task;
            scanner.Cancel();
            cancelled.SetResult();
        });

        var line = new byte[geometry.BytesPerLine];
        Assert.Equal(ScanLineResult.Delivered, scanner.ScanReadLine(line));
        lineRead.SetResult();
        await cancelled.Task;
        await stopper;

        Assert.Equal(ScanLineResult.Cancelled, scanner.ScanReadLine(line));
        Assert.Equal(ScanStatus.Ok, scanner.ScanEnd());
    }

    // =========================================================================
    // Callback-ovi - zbog cega ovaj sloj i postoji
    // =========================================================================

    [Fact]
    public void ACollectionDuringAScanDoesNotKillTheCallback()
    {
        // OVO JE NAJVAZNIJI TEST U FAJLU.
        //
        // Delegat prosledjen native strani mora ostati ziv dok ga ta strana
        // moze pozvati. GC to ne zna: vidi da lokalnu promenljivu vise niko ne
        // cita i pokupi je, a native strana onda skoci na oslobodjenu memoriju.
        //
        // Ishod je rusenje koje se desava retko, kod korisnika, i nikada u
        // testu - jer test ne stigne da izazove sakupljanje na pravom mestu.
        // Ovde se sakupljanje izaziva NAMERNO, izmedju svaka dva reda.
        using Scanner scanner = OpenReady();

        var messages = new ConcurrentBag<string>();

        // Lambda se NIGDE ne cuva sa ove strane. Jedino sto je drzi u zivotu je
        // GCHandle unutar Interop-a - i bas to se ovde meri.
        scanner.SetLog((level, message) => messages.Add($"{level}:{message}"));

        scanner.Warmup(TimeSpan.FromMilliseconds(20));
        ScanGeometry geometry = scanner.ScanBegin(SmallScan() with { Height = 40 });

        var line = new byte[geometry.BytesPerLine];
        int delivered = 0;
        try
        {
            while (scanner.ScanReadLine(line) == ScanLineResult.Delivered)
            {
                ++delivered;
                GC.Collect();
                GC.WaitForPendingFinalizers();
                GC.Collect();
            }
        }
        finally
        {
            scanner.ScanEnd();
        }
        Assert.Equal(geometry.Lines, delivered);

        // Sada, BEZ ponovnog prijavljivanja, izazovi gresku.
        //
        // Prva verzija ovog testa je ovde ponovo zvala SetLog - i time merila
        // sveze registrovan callback umesto onog koji je prezivio sakupljanje.
        // Tako napisan, prolazio je i kada je GCHandle bio slab.
        Assert.Throws<ScannerException>(() =>
            scanner.ScanBegin(SmallScan() with { BitsPerChannel = 12 }));

        Assert.NotEmpty(messages);
    }

    [Fact]
    public void TheProgressCallbackCanStopAWarmup()
    {
        using Scanner scanner = OpenReady();

        int calls = 0;
        ScanStatus status = scanner.Warmup(TimeSpan.FromMilliseconds(400), percent =>
        {
            Assert.InRange(percent, 0, 100);
            return ++calls < 3;  // treci poziv trazi prekid
        });

        Assert.Equal(ScanStatus.Cancelled, status);
        Assert.Equal(3, calls);
    }

    [Fact]
    public void AnExceptionInsideACallbackSurfacesHereInsteadOfKillingTheProcess()
    {
        using Scanner scanner = OpenReady();

        // Izuzetak koji predje granicu natrag u native kod rusi proces bez
        // ijedne poruke - .NET runtime ga tamo ne moze uhvatiti. Zato se pamti
        // i baca sa ove strane.
        var thrown = Assert.Throws<InvalidOperationException>(() =>
            scanner.Warmup(TimeSpan.FromMilliseconds(200),
                           _ => throw new InvalidOperationException("iz callback-a")));

        Assert.Equal("iz callback-a", thrown.Message);
    }

    [Fact]
    public void TurningTheLogOffReallyTurnsItOff()
    {
        using Scanner scanner = OpenReady();

        var lines = new List<string>();
        scanner.SetLog((_, message) => lines.Add(message));

        Assert.Throws<ScannerException>(() => scanner.ScanBegin(SmallScan() with { BitsPerChannel = 12 }));
        int afterFirst = lines.Count;
        Assert.True(afterFirst > 0, "greska nije stigla u dnevnik");

        scanner.SetLog(null);
        Assert.Throws<ScannerException>(() => scanner.ScanBegin(SmallScan() with { BitsPerChannel = 12 }));
        Assert.Equal(afterFirst, lines.Count);
    }

    [Fact]
    public void ReplacingTheLogTwiceDoesNotLeaveADanglingPointer()
    {
        using Scanner scanner = OpenReady();

        // Stari se otkaci PRE nego sto se novi zakaci, i tek onda oslobodi.
        // Obrnut redosled bi native strani ostavio pokazivac na oslobodjen
        // GCHandle - tacno onaj otkaz koji ovaj sloj postoji da spreci.
        for (int i = 0; i < 10; ++i)
        {
            int captured = i;
            var seen = new List<string>();
            scanner.SetLog((_, message) => seen.Add($"{captured}:{message}"));
            GC.Collect();
            GC.WaitForPendingFinalizers();
        }

        var last = new List<string>();
        scanner.SetLog((_, message) => last.Add(message));
        Assert.Throws<ScannerException>(() => scanner.ScanBegin(SmallScan() with { BitsPerChannel = 12 }));
        Assert.NotEmpty(last);
    }

    // =========================================================================
    // Bezbednost i ono sto jos ne postoji
    // =========================================================================

    [Fact]
    public void WarmupIsRefusedBelowLevelTwo()
    {
        using Scanner scanner = OpenReady(level: 1);
        var exception = Assert.Throws<ScannerException>(() =>
            scanner.Warmup(TimeSpan.FromMilliseconds(20)));

        Assert.Equal(ScanStatus.SafetyViolation, exception.Status);
    }

    [Fact]
    public void ScanningIsRefusedBelowLevelFive()
    {
        using Scanner scanner = OpenReady(level: 1);
        var exception = Assert.Throws<ScannerException>(() => scanner.ScanBegin(SmallScan()));

        // Na nivou 1 lampa se ne sme ni upaliti, pa prolaz pada na provere
        // sesije - ne na plafon. Poruka to i kaze.
        Assert.NotEqual(ScanStatus.Ok, exception.Status);
        Assert.NotEmpty(exception.Detail);
    }

    [Fact]
    public void HomeSaysWhatIsMissingInsteadOfPretending()
    {
        using Scanner scanner = OpenReady();
        if (scanner.EffectiveSafetyLevel < 3)
        {
            return;  // plafon build-a je ispod 3; to pokriva test iznad
        }

        var exception = Assert.Throws<ScannerException>(() => scanner.Home());
        Assert.Equal(ScanStatus.NotImplemented, exception.Status);

        // Razlog mora biti naveden. "Nije implementirano" bez razloga je
        // izvestaj koji se ne moze upotrebiti.
        Assert.Contains("Head_Relocate", exception.Detail);
    }

    [Fact]
    public void HomeBelowLevelThreeSaysSafetyNotUnimplemented()
    {
        using Scanner scanner = OpenReady(level: 1);
        var exception = Assert.Throws<ScannerException>(() => scanner.Home());

        // Paketu sa plafonom 1 tacan odgovor je "ovaj paket to ne sme", a ne
        // "nije implementirano".
        Assert.Equal(ScanStatus.SafetyViolation, exception.Status);
    }

    // =========================================================================
    // Trag
    // =========================================================================

    [Fact]
    public void WithoutRecordingTheTraceIsRefusedNotEmpty()
    {
        using Scanner scanner = OpenReady();
        Assert.Equal(0, scanner.TraceCount);

        var exception = Assert.Throws<ScannerException>(() =>
            scanner.WriteTrace(Path.Combine(Path.GetTempPath(), "nepostojeci.trace")));
        Assert.Equal(ScanStatus.InvalidState, exception.Status);
    }

    [Fact]
    public void RecordingCapturesTransfersAndWritesThemOut()
    {
        using Scanner scanner = OpenReady(trace: true);
        Assert.True(scanner.TraceCount > 0, "identify nije zabelezen");

        string path = Path.Combine(Path.GetTempPath(),
                                   $"g2710-interop-{Guid.NewGuid():N}.trace");
        try
        {
            scanner.WriteTrace(path);
            Assert.True(File.Exists(path));
            Assert.NotEmpty(File.ReadAllText(path));
        }
        finally
        {
            File.Delete(path);
        }
    }
}
