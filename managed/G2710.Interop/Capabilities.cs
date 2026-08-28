using System.Collections.Generic;
using System.Linq;
using System.Text.Json;
using System.Text.Json.Serialization;

namespace G2710.Interop;

/// <summary>Koliko se sme tvrditi o jednoj mogucnosti.</summary>
public enum ValidationLevel
{
    NotImplemented,

    /// <summary>Kod postoji.</summary>
    Implemented,

    /// <summary>Ponasa se kao hp3900 referenca.</summary>
    ReferenceValidated,

    /// <summary>Potvrdjeno na fizickom uredjaju. Samo se ovo sme ponuditi.</summary>
    HardwareValidated,
}

/// <summary>Jedna rezolucija iz tabele.</summary>
public sealed record ResolutionCapability
{
    public int Dpi { get; init; }

    /// <summary><c>native</c> ili <c>resize</c>.</summary>
    public string Origin { get; init; } = string.Empty;

    public ValidationLevel Level { get; init; }

    /// <summary>Ako je smanjivanje: iz koje rezolucije.</summary>
    public int SourceDpi { get; init; }

    /// <summary>Rezolucija na kojoj se STVARNO skenira.</summary>
    public int NativeDpi { get; init; }

    /// <summary><c>hardware</c> ili <c>software</c> poravnanje kanala.</summary>
    public string Alignment { get; init; } = string.Empty;

    /// <summary>Sme li se ponuditi krajnjem korisniku.</summary>
    public bool Advertisable { get; init; }

    /// <summary>Kratko obrazlozenje; prazno ako ga nema.</summary>
    public string Note { get; init; } = string.Empty;
}

public sealed record DepthCapability
{
    public int Bits { get; init; }
    public ValidationLevel Level { get; init; }
    public string Note { get; init; } = string.Empty;
}

/// <summary>
/// Sta uredjaj ume, i koliko toga smemo da tvrdimo.
/// </summary>
/// <remarks>
/// Cita se iz native strane, a ne prepisuje ovde. Prepisan spisak bi se razisao
/// sa jezgrom prvi put kada neka rezolucija promeni status - i aplikacija bi
/// nudila nesto sto drajver odbija, ili obrnuto.
///
/// Racun je statican i ne dodiruje uredjaj: radi i kada skenera nema.
/// </remarks>
public sealed record ScannerCapabilities
{
    public string Device { get; init; } = string.Empty;
    public IReadOnlyList<ResolutionCapability> Resolutions { get; init; } = [];
    public IReadOnlyList<DepthCapability> Depths { get; init; } = [];

    /// <summary>
    /// Rezolucije koje se smeju ponuditi krajnjem korisniku.
    /// </summary>
    /// <remarks>
    /// Na dan pisanja: PRAZNO. To nije propust nego pravilo - oglasava se
    /// iskljucivo ono sto je proslo hardversku kvalifikaciju, a skener jos nije
    /// bio prikljucen. Aplikacija mora imati odgovor na taj slucaj.
    /// </remarks>
    public IReadOnlyList<int> Advertisable { get; init; } = [];

    /// <summary>Sve sto se moze izvrsiti, ukljucujuci neoglaseno.</summary>
    public IReadOnlyList<int> Executable =>
        Resolutions.Where(r => r.NativeDpi > 0).Select(r => r.Dpi).ToList();

    /// <summary>Procitaj iz native strane.</summary>
    public static ScannerCapabilities Read()
    {
        string json = Utf8.Read((buffer, capacity) =>
            NativeMethods.Capabilities(buffer, capacity));
        return Parse(json);
    }

    /// <summary>
    /// Rasclani izvestaj. Izdvojeno da se moze testirati bez native strane.
    /// </summary>
    public static ScannerCapabilities Parse(string json)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(json);

        Dto? dto = JsonSerializer.Deserialize<Dto>(json, Options);
        if (dto is null)
        {
            throw new JsonException("Izvestaj o mogucnostima je prazan.");
        }

        return new ScannerCapabilities
        {
            Device = dto.Device ?? string.Empty,
            Resolutions = (dto.Resolutions ?? []).Select(r => new ResolutionCapability
            {
                Dpi = r.Dpi,
                Origin = r.Origin ?? string.Empty,
                Level = ToLevel(r.Level),
                SourceDpi = r.SourceDpi,
                NativeDpi = r.NativeDpi,
                Alignment = r.Alignment ?? string.Empty,
                Advertisable = r.Advertisable,
                Note = r.Note ?? string.Empty,
            }).ToList(),
            Depths = (dto.Depths ?? []).Select(d => new DepthCapability
            {
                Bits = d.Bits,
                Level = ToLevel(d.Level),
                Note = d.Note ?? string.Empty,
            }).ToList(),
            Advertisable = dto.Advertisable ?? [],
        };
    }

    // Nivo stize kao niska iz C++ `toString(ValidationLevel)`. Nepoznata
    // vrednost postaje NotImplemented, ne izuzetak: novo ime u jezgru ne sme
    // srusiti aplikaciju, nego samo znaciti "ne nudi se".
    private static ValidationLevel ToLevel(string? text) => text switch
    {
        "IMPLEMENTED" => ValidationLevel.Implemented,
        "REFERENCE_VALIDATED" => ValidationLevel.ReferenceValidated,
        "HARDWARE_VALIDATED" => ValidationLevel.HardwareValidated,
        _ => ValidationLevel.NotImplemented,
    };

    private static readonly JsonSerializerOptions Options = new()
    {
        PropertyNameCaseInsensitive = true,
    };

    private sealed class Dto
    {
        [JsonPropertyName("device")] public string? Device { get; set; }
        [JsonPropertyName("resolutions")] public List<ResolutionDto>? Resolutions { get; set; }
        [JsonPropertyName("depths")] public List<DepthDto>? Depths { get; set; }
        [JsonPropertyName("advertisable")] public List<int>? Advertisable { get; set; }
    }

    private sealed class ResolutionDto
    {
        [JsonPropertyName("dpi")] public int Dpi { get; set; }
        [JsonPropertyName("origin")] public string? Origin { get; set; }
        [JsonPropertyName("level")] public string? Level { get; set; }
        [JsonPropertyName("sourceDpi")] public int SourceDpi { get; set; }
        [JsonPropertyName("nativeDpi")] public int NativeDpi { get; set; }
        [JsonPropertyName("alignment")] public string? Alignment { get; set; }
        [JsonPropertyName("advertisable")] public bool Advertisable { get; set; }
        [JsonPropertyName("note")] public string? Note { get; set; }
    }

    private sealed class DepthDto
    {
        [JsonPropertyName("bits")] public int Bits { get; set; }
        [JsonPropertyName("level")] public string? Level { get; set; }
        [JsonPropertyName("note")] public string? Note { get; set; }
    }
}
