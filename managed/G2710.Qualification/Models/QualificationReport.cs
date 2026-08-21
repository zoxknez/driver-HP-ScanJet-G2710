using System.Collections.Generic;
using System.Text.Json;
using System.Text.Json.Serialization;

namespace G2710.Qualification.Models;

/// <summary>
/// Ishod jedne provere, onako kako ga g2710ctl prijavljuje.
/// </summary>
/// <remarks>
/// Imena su ista kao rec u JSON-u, pa se ne prevode dvaput. Nepoznata rec se
/// NE preslikava u nesto poznato - to bi sakrilo da alat i wizard vise ne
/// govore isto.
/// </remarks>
public enum CheckOutcome
{
    Unknown,
    Pass,
    Fail,
    Blocked,
    Pending,
    Ask,
}

/// <summary>Odgovor koji je korisnik dao na pitanje.</summary>
public enum UserAnswer
{
    Unanswered,
    Yes,
    No,
}

public sealed class CheckResult
{
    [JsonPropertyName("id")]
    public string Id { get; set; } = string.Empty;

    [JsonPropertyName("name")]
    public string Name { get; set; } = string.Empty;

    [JsonPropertyName("result")]
    public string ResultWord { get; set; } = string.Empty;

    [JsonPropertyName("level")]
    public int Level { get; set; }

    [JsonPropertyName("detail")]
    public string Detail { get; set; } = string.Empty;

    [JsonPropertyName("question")]
    public string Question { get; set; } = string.Empty;

    [JsonPropertyName("date")]
    public string Date { get; set; } = string.Empty;

    /// <summary>Odgovor korisnika; vazi samo kada je <see cref="Outcome"/> Ask.</summary>
    [JsonPropertyName("answer")]
    public string AnswerWord { get; set; } = string.Empty;

    [JsonIgnore]
    public CheckOutcome Outcome => ParseOutcome(ResultWord);

    [JsonIgnore]
    public UserAnswer Answer
    {
        get => AnswerWord switch
        {
            "YES" => UserAnswer.Yes,
            "NO" => UserAnswer.No,
            _ => UserAnswer.Unanswered,
        };
        set => AnswerWord = value switch
        {
            UserAnswer.Yes => "YES",
            UserAnswer.No => "NO",
            _ => string.Empty,
        };
    }

    public static CheckOutcome ParseOutcome(string word) => word switch
    {
        "PASS" => CheckOutcome.Pass,
        "FAIL" => CheckOutcome.Fail,
        "BLOCKED" => CheckOutcome.Blocked,
        "PENDING" => CheckOutcome.Pending,
        "ASK" => CheckOutcome.Ask,
        _ => CheckOutcome.Unknown,
    };
}

public sealed class ReportSummary
{
    [JsonPropertyName("pass")] public int Pass { get; set; }
    [JsonPropertyName("fail")] public int Fail { get; set; }
    [JsonPropertyName("blocked")] public int Blocked { get; set; }
    [JsonPropertyName("pending")] public int Pending { get; set; }
    [JsonPropertyName("ask")] public int Ask { get; set; }
}

public sealed class QualificationReport
{
    [JsonPropertyName("device")] public string Device { get; set; } = string.Empty;
    [JsonPropertyName("timestamp")] public string Timestamp { get; set; } = string.Empty;
    [JsonPropertyName("safetyCeiling")] public int SafetyCeiling { get; set; }
    [JsonPropertyName("effectiveLevel")] public int EffectiveLevel { get; set; }
    [JsonPropertyName("summary")] public ReportSummary Summary { get; set; } = new();
    [JsonPropertyName("tests")] public List<CheckResult> Tests { get; set; } = new();

    private static readonly JsonSerializerOptions Options = new()
    {
        PropertyNameCaseInsensitive = true,
        WriteIndented = true,
    };

    /// <summary>
    /// Ucitaj izvestaj. Vraca null ako tekst nije izvestaj - prazan fajl,
    /// polovicno upisan, ili nesto sasvim drugo.
    /// </summary>
    /// <remarks>
    /// Izuzetak se ovde NE pusta dalje. Wizard mora umeti da kaze "alat nije
    /// vratio izvestaj" umesto da se srusi pred korisnikom koji ne zna sta je
    /// JSON.
    /// </remarks>
    public static QualificationReport? TryParse(string json)
    {
        if (string.IsNullOrWhiteSpace(json))
        {
            return null;
        }

        try
        {
            var report = JsonSerializer.Deserialize<QualificationReport>(json, Options);

            // Izvestaj bez ijedne provere nije izvestaj nego prazan objekat.
            return report is { Tests.Count: > 0 } ? report : null;
        }
        catch (JsonException)
        {
            return null;
        }
    }

    public string ToJson() => JsonSerializer.Serialize(this, Options);

    /// <summary>Provere na koje masina ne moze da odgovori.</summary>
    public IEnumerable<CheckResult> Questions()
    {
        foreach (var test in Tests)
        {
            if (test.Outcome == CheckOutcome.Ask)
            {
                yield return test;
            }
        }
    }

    /// <summary>Da li je sve odgovoreno i nista nije palo.</summary>
    public bool IsComplete()
    {
        foreach (var test in Tests)
        {
            if (test.Outcome == CheckOutcome.Ask && test.Answer == UserAnswer.Unanswered)
            {
                return false;
            }
        }
        return true;
    }

    /// <summary>
    /// Koliko je stvarno palo, racunajuci i pitanja na koja je odgovoreno NE.
    /// </summary>
    /// <remarks>
    /// "Lampa ne svetli" je PAD, ma sta registar tvrdio. Sazetak iz alata to
    /// ne moze da zna jer u trenutku pisanja odgovor jos ne postoji.
    /// </remarks>
    public int FailureCount()
    {
        var failures = 0;
        foreach (var test in Tests)
        {
            if (test.Outcome == CheckOutcome.Fail)
            {
                failures++;
            }
            else if (test.Outcome == CheckOutcome.Ask && test.Answer == UserAnswer.No)
            {
                failures++;
            }
        }
        return failures;
    }
}
