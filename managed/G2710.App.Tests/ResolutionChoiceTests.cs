using System.Linq;
using G2710.App;
using G2710.Interop;
using Xunit;

namespace G2710.App.Tests;

/// <summary>
/// Spisak rezolucija dolazi iz jezgra, a ne iz stavki otkucanih u XAML-u.
/// </summary>
/// <remarks>
/// Prva verzija aplikacije je nudila tri vrednosti - 150, 300 i 600 - dok
/// jezgro poznaje devet, svaku sa svojim statusom i razlogom. Takav spisak se
/// razilazi sa drajverom prvi put kada neka rezolucija promeni status: tada
/// aplikacija nudi nesto sto drajver odbija, ili krije nesto sto radi.
/// </remarks>
public sealed class ResolutionChoiceTests
{
    // Prava tabela iz native strane. Bez laznjaka - ono sto se ovde meri je
    // bas to da se dve strane slazu.
    private static ScannerCapabilities Real() => Scanner.Capabilities();

    [Fact]
    public void The_core_offers_more_than_the_three_values_the_ui_used_to_hardcode()
    {
        var choices = ResolutionChoice.From(Real(), includeUnqualified: true);

        Assert.True(choices.Count > 3,
            $"jezgro nudi {choices.Count} rezolucija; lista od tri je bila prepisana rukom");
        Assert.Contains(choices, c => c.Dpi == 150);
        Assert.Contains(choices, c => c.Dpi == 300);
        Assert.Contains(choices, c => c.Dpi == 600);
    }

    [Fact]
    public void Every_offered_resolution_can_actually_be_planned()
    {
        // Vrednost bez plana se ne moze skenirati; nuditi je znaci obecati
        // dugme koje pada tek kada se pritisne.
        foreach (var choice in ResolutionChoice.From(Real(), includeUnqualified: true))
        {
            var geometry = Scanner.Plan(new ScanSettings
            {
                Resolution = choice.Dpi,
                ColorMode = ScanColorMode.Color,
                BitsPerChannel = 8,
                AllowUnqualified = true,
            });
            Assert.Equal(choice.NativeDpi, geometry.NativeResolution);
        }
    }

    [Fact]
    public void Nothing_is_advertisable_until_the_hardware_says_so()
    {
        // Pravilo iz MASTER plana. Na dan pisanja skener nije bio prikljucen,
        // pa je ponuda za krajnjeg korisnika PRAZNA - i aplikacija mora imati
        // odgovor na taj slucaj, umesto da prikaze prazan spisak.
        var advertised = ResolutionChoice.From(Real(), includeUnqualified: false);
        var all = ResolutionChoice.From(Real(), includeUnqualified: true);

        Assert.Empty(advertised);
        Assert.NotEmpty(all);
    }

    [Fact]
    public void An_unverified_resolution_says_so_instead_of_being_offered_silently()
    {
        var choices = ResolutionChoice.From(Real(), includeUnqualified: true);

        foreach (var choice in choices.Where(c => !c.Advertisable))
        {
            Assert.False(string.IsNullOrWhiteSpace(choice.Caveat),
                $"{choice.Dpi} dpi se nudi bez ijedne reci o tome da nije potvrdjeno");
        }
    }

    [Fact]
    public void A_verified_resolution_would_carry_no_warning()
    {
        // Danas ovakve nema; kada H8 prodje, upozorenje mora nestati samo od
        // sebe - bez ijedne izmene u UI-ju.
        var verified = new ResolutionChoice
        {
            Dpi = 300,
            NativeDpi = 300,
            Level = ValidationLevel.HardwareValidated,
            Advertisable = true,
            Note = "nebitna napomena",
        };
        Assert.Equal(string.Empty, verified.Caveat);
    }

    [Fact]
    public void A_resized_resolution_reports_where_it_really_scans()
    {
        // 200 dpi nema svoj red u tabeli hardvera - skenira se na 300 pa
        // smanjuje. Korisnik to mora moci da sazna.
        var choices = ResolutionChoice.From(Real(), includeUnqualified: true);
        var resized = choices.FirstOrDefault(c => c.Dpi == 200);

        Assert.NotNull(resized);
        Assert.True(resized!.IsResized);
        Assert.Equal(300, resized.NativeDpi);
    }

    [Fact]
    public void The_view_model_starts_on_a_resolution_that_exists_in_the_list()
    {
        // Podrazumevana vrednost koja nije u listi ostavlja prazan ComboBox i
        // "Skeniraj" koje ne radi - kvar koji izgleda kao pokvaren program.
        var model = new MainViewModel();

        Assert.NotEmpty(model.Resolutions);
        Assert.NotNull(model.SelectedResolution);
        Assert.Contains(model.SelectedResolution!, model.Resolutions);
        Assert.Equal(model.SelectedResolution!.Dpi, model.Resolution);
    }

    [Fact]
    public void Choosing_a_resolution_updates_what_the_user_is_told()
    {
        var model = new MainViewModel();
        var unverified = model.Resolutions.First(r => !r.Advertisable);

        model.SelectedResolution = unverified;

        Assert.Equal(unverified.Dpi, model.Resolution);
        Assert.True(model.HasResolutionCaveat);
        Assert.Equal(unverified.Caveat, model.SelectedResolutionCaveat);
    }
}
