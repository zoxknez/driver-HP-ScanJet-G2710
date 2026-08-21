using System.Linq;
using G2710.Qualification.Models;
using Xunit;

namespace G2710.Qualification.Tests;

/// <summary>
/// Citanje izvestaja koji je napisao g2710ctl.
/// </summary>
/// <remarks>
/// Wizard mora prezivati izvestaj koji NIJE onakav kakav ocekuje - prazan,
/// polovicno upisan, ili iz novije verzije alata. Pad pred korisnikom koji ne
/// zna sta je JSON je najgori moguci ishod.
/// </remarks>
public class ReportTests
{
    private const string Sample = """
    {
      "device": "03F0-2805",
      "timestamp": "2026-08-21T10:00:00",
      "safetyCeiling": 5,
      "effectiveLevel": 5,
      "summary": {"pass": 2, "fail": 1, "blocked": 0, "pending": 1, "ask": 1},
      "tests": [
        {"id": "H1.1", "name": "USB identitet", "result": "PASS", "level": 1,
         "date": "2026-08-21T10:00:00", "detail": "03F0:2805"},
        {"id": "H2.1", "name": "Registri", "result": "PASS", "level": 1,
         "date": "2026-08-21T10:00:00", "detail": "1818 bajtova"},
        {"id": "H3.2", "name": "Status lampe", "result": "FAIL", "level": 2,
         "date": "2026-08-21T10:00:00", "detail": "upisano ukljuceno, procitano iskljuceno"},
        {"id": "H4.1", "name": "Home", "result": "PENDING", "level": 3,
         "date": "2026-08-21T10:00:00", "detail": "ceka port Head_Relocate"},
        {"id": "H3.3", "name": "Lampa svetli", "result": "ASK", "level": 2,
         "date": "2026-08-21T10:00:00", "question": "Da li lampa svetli?"}
      ]
    }
    """;

    [Fact]
    public void ParsesEveryField()
    {
        var report = QualificationReport.TryParse(Sample);

        Assert.NotNull(report);
        Assert.Equal("03F0-2805", report!.Device);
        Assert.Equal(5, report.SafetyCeiling);
        Assert.Equal(5, report.EffectiveLevel);
        Assert.Equal(5, report.Tests.Count);
        Assert.Equal(1, report.Summary.Fail);
    }

    [Fact]
    public void MapsResultWordsToOutcomes()
    {
        var report = QualificationReport.TryParse(Sample)!;

        Assert.Equal(CheckOutcome.Pass, report.Tests[0].Outcome);
        Assert.Equal(CheckOutcome.Fail, report.Tests[2].Outcome);
        Assert.Equal(CheckOutcome.Pending, report.Tests[3].Outcome);
        Assert.Equal(CheckOutcome.Ask, report.Tests[4].Outcome);
    }

    // Nepoznata rec se NE preslikava u nesto poznato. Da se preslikava, alat i
    // wizard bi mogli da se raziidju a da to niko ne primeti.
    [Fact]
    public void UnknownResultWordStaysUnknown()
    {
        Assert.Equal(CheckOutcome.Unknown, CheckResult.ParseOutcome("NESTO_NOVO"));
        Assert.Equal(CheckOutcome.Unknown, CheckResult.ParseOutcome(""));
        Assert.Equal(CheckOutcome.Unknown, CheckResult.ParseOutcome("pass"));
    }

    [Theory]
    [InlineData("")]
    [InlineData("   ")]
    [InlineData("ovo nije json")]
    [InlineData("{")]
    [InlineData("{\"tests\": []}")]
    [InlineData("[1, 2, 3]")]
    public void RefusesAnythingThatIsNotAReport(string text)
    {
        Assert.Null(QualificationReport.TryParse(text));
    }

    [Fact]
    public void QuestionsAreOnlyTheOnesThatAskSomething()
    {
        var report = QualificationReport.TryParse(Sample)!;
        var questions = report.Questions().ToList();

        Assert.Single(questions);
        Assert.Equal("H3.3", questions[0].Id);
    }

    // Izvestaj nije gotov dok korisnik ne odgovori. Bez toga bi se poslao
    // izvestaj u kome pise "pitanje", a to nikome ne pomaze.
    [Fact]
    public void IsNotCompleteUntilEveryQuestionIsAnswered()
    {
        var report = QualificationReport.TryParse(Sample)!;
        Assert.False(report.IsComplete());

        report.Questions().First().Answer = UserAnswer.Yes;
        Assert.True(report.IsComplete());
    }

    // "Lampa ne svetli" je PAD, ma sta registar tvrdio. Sazetak iz alata to ne
    // moze da zna jer u trenutku pisanja odgovor jos ne postoji.
    [Fact]
    public void AnsweringNoCountsAsAFailure()
    {
        var report = QualificationReport.TryParse(Sample)!;
        Assert.Equal(1, report.FailureCount());

        report.Questions().First().Answer = UserAnswer.No;
        Assert.Equal(2, report.FailureCount());

        report.Questions().First().Answer = UserAnswer.Yes;
        Assert.Equal(1, report.FailureCount());
    }

    [Fact]
    public void AnswersSurviveARoundTripThroughJson()
    {
        var report = QualificationReport.TryParse(Sample)!;
        report.Questions().First().Answer = UserAnswer.No;

        var again = QualificationReport.TryParse(report.ToJson());

        Assert.NotNull(again);
        Assert.Equal(UserAnswer.No, again!.Questions().First().Answer);
        Assert.Equal(2, again.FailureCount());
    }

    // Alat novije verzije sme dodati polja. Wizard ih ignorise umesto da padne.
    [Fact]
    public void UnknownFieldsAreIgnored()
    {
        const string withExtras = """
        {
          "device": "X", "timestamp": "T", "safetyCeiling": 1, "effectiveLevel": 1,
          "necegaNovog": {"a": 1},
          "tests": [{"id": "H1.1", "name": "n", "result": "PASS", "level": 1,
                     "date": "T", "buducePolje": 42}]
        }
        """;

        var report = QualificationReport.TryParse(withExtras);
        Assert.NotNull(report);
        Assert.Single(report!.Tests);
    }
}
