using System;
using System.Collections.Generic;
using System.Linq;
using G2710.Interop;

namespace G2710.App;

/// <summary>Jedna rezolucija onako kako je korisnik vidi u listi.</summary>
/// <remarks>
/// Nastaje iz tabele koju daje jezgro, a NE iz spiska otkucanog u XAML-u.
///
/// Prva verzija je nudila tri stavke - 150, 300 i 600 - dok jezgro poznaje
/// devet, svaku sa svojim statusom i razlogom. Takav spisak se razilazi sa
/// drajverom prvi put kada neka rezolucija promeni status, i onda aplikacija
/// nudi nesto sto drajver odbija, ili krije nesto sto radi.
/// </remarks>
public sealed record ResolutionChoice
{
    public required int Dpi { get; init; }

    /// <summary>Rezolucija na kojoj se STVARNO skenira.</summary>
    public required int NativeDpi { get; init; }

    public required ValidationLevel Level { get; init; }

    /// <summary>Sme li se ponuditi kao gotov proizvod.</summary>
    public required bool Advertisable { get; init; }

    /// <summary>Obrazlozenje iz tabele; prazno ako ga nema.</summary>
    public string Note { get; init; } = string.Empty;

    /// <summary>Tekst u listi.</summary>
    public string Label => $"{Dpi} dpi";

    /// <summary>
    /// Sta korisnik treba da zna pre nego sto izabere ovu vrednost.
    /// </summary>
    /// <remarks>
    /// Prazno znaci "nema sta da se doda". Sve ostalo se prikazuje pored
    /// izbora - tiho nudjenje neprovererene vrednosti je upravo ono sto ovaj
    /// projekat ne radi.
    /// </remarks>
    public string Caveat
    {
        get
        {
            if (Advertisable)
            {
                return string.Empty;
            }
            var reason = Level switch
            {
                ValidationLevel.ReferenceValidated =>
                    "Ponaša se kao referentni drajver, ali još nije potvrđeno na uređaju.",
                ValidationLevel.Implemented =>
                    "Kod postoji, ali nije potvrđen ni na referenci ni na uređaju.",
                ValidationLevel.NotImplemented =>
                    "Nije implementirano.",
                _ => string.Empty,
            };
            return string.IsNullOrEmpty(Note) ? reason : $"{reason} {Note}";
        }
    }

    /// <summary>Skenira li se zaista na drugoj rezoluciji pa smanjuje.</summary>
    public bool IsResized => NativeDpi > 0 && NativeDpi != Dpi;

    /// <summary>
    /// Napravi listu iz tabele jezgra.
    /// </summary>
    /// <param name="capabilities">Tabela iz <see cref="Scanner.Capabilities"/>.</param>
    /// <param name="includeUnqualified">
    /// Da li se nude i vrednosti koje hardver nije potvrdio. Za dijagnostiku
    /// da; ono što se nudi krajnjem korisniku kao gotov proizvod - ne.
    /// </param>
    public static IReadOnlyList<ResolutionChoice> From(ScannerCapabilities capabilities,
                                                       bool includeUnqualified)
    {
        ArgumentNullException.ThrowIfNull(capabilities);

        return capabilities.Resolutions
            .Where(r => r.NativeDpi > 0)               // bez plana se ne moze skenirati
            .Where(r => includeUnqualified || r.Advertisable)
            .Select(r => new ResolutionChoice
            {
                Dpi = r.Dpi,
                NativeDpi = r.NativeDpi,
                Level = r.Level,
                Advertisable = r.Advertisable,
                Note = r.Note,
            })
            .ToList();
    }

    public override string ToString() => Label;
}
