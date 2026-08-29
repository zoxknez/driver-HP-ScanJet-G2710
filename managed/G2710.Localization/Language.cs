using System;
using System.Globalization;
using System.IO;
using System.Resources;

namespace G2710.Localization;

/// <summary>
/// Koji jezik program govori.
/// </summary>
/// <remarks>
/// ENGLESKI JE PRIMARAN, i to nije stvar ukusa nego posledica: engleski je
/// NEUTRALNI resurs, ugradjen u samu biblioteku. Srpski je satelit koji se
/// ucitava pored nje. Ako satelit nedostane - a nedostaje kad god paket nije
/// potpun - program i dalje govori, samo engleski. Obrnut raspored bi dao
/// prazan prozor.
///
/// REDOSLED ODLUKE, od najjaceg ka najslabijem:
///
///   1. sto je program izricito postavio (Override) - testovi i prekidac
///      komandne linije
///   2. sto je korisnik izabrao PRI INSTALACIJI, zapisano u registru
///   3. sto pise u language.txt pored programa - prenosivi paket koji se ne
///      instalira nema registar u koji bi upisao izbor
///   4. jezik Windows-a, ako je srpski
///   5. engleski
///
/// Instalacija pise izbor u HKLM; kvalifikacioni ZIP pise language.txt; korak
/// 4 postoji da bi i program koji je samo prekopiran pogodio ocekivani jezik.
/// </remarks>
public static class Language
{
    /// <summary>Gde instalacija upisuje izbor korisnika.</summary>
    public const string RegistryPath = @"SOFTWARE\G2710";
    public const string RegistryValue = "Language";

    /// <summary>Gde prenosivi paket upisuje izbor korisnika.</summary>
    /// <remarks>
    /// Kvalifikacioni ZIP se ne instalira - raspakuje se i pokrene. Registar
    /// tada niko nije upisao, pa izbor stoji u fajlu pored programa.
    /// </remarks>
    public const string PortableFile = "language.txt";

    /// <summary>Jezici koje program zaista ima.</summary>
    public static readonly string[] Supported = ["en", "sr"];

    private static CultureInfo? _override;

    /// <summary>
    /// Izricito postavljen jezik. <c>null</c> vraca odlucivanje na uobicajen
    /// redosled.
    /// </summary>
    /// <remarks>
    /// Postoji da bi testovi mogli proveriti OBA jezika bez menjanja
    /// racunara, i da bi alat mogao primiti prekidac komandne linije.
    /// </remarks>
    public static CultureInfo? Override
    {
        get => _override;
        set
        {
            _override = value;
            Current = Resolve();
        }
    }

    /// <summary>Jezik koji se trenutno koristi. Nikada <c>null</c>.</summary>
    public static CultureInfo Current { get; private set; } = Resolve();

    /// <summary>Ponovo odluči — posle promene postavke ili u testu.</summary>
    public static void Refresh() => Current = Resolve();

    /// <summary>
    /// Odluka po zapisanom redosledu.
    /// </summary>
    /// <param name="installed">
    /// Sto je zapisano pri instalaciji; <c>null</c> ako nista nije zapisano.
    /// Izdvojeno kao parametar da se ceo redosled moze testirati bez registra.
    /// </param>
    /// <param name="system">Jezik operativnog sistema.</param>
    /// <param name="portable">
    /// Sto pise u <see cref="PortableFile"/>; <c>null</c> ako fajla nema.
    /// </param>
    public static CultureInfo Decide(CultureInfo? explicitChoice, string? installed,
                                     string? portable, CultureInfo? system)
    {
        if (explicitChoice is not null)
        {
            return Normalise(explicitChoice);
        }
        // ZAPISAN izbor "en" je ODLUKA, ne cutanje.
        //
        // Prva verzija je odbacivala "en" jer engleski nije satelit - a
        // instalater ga upisuje uvek, i podrazumevano. Posledica: korisnik koji
        // je u instalaciji izabrao English dobijao bi srpski cim mu je Windows
        // srpski, jer bi njegov izbor bio preskocen kao "nije receno nista".
        if (Offered(installed) is { } chosen)
        {
            return chosen;
        }
        if (Offered(portable) is { } fromFile)
        {
            return fromFile;
        }
        // Jezik sistema je NAGOVESTAJ, ne izbor: uzima se samo ako je srpski.
        // Engleski Windows i tako vodi na engleski, pa se ne razlikuje od
        // poslednje odbrane.
        if (system is not null && IsSatellite(system.TwoLetterISOLanguageName))
        {
            return new CultureInfo(system.TwoLetterISOLanguageName);
        }

        // Engleski je i podrazumevan i poslednja odbrana.
        return CultureInfo.InvariantCulture;
    }

    /// <summary>
    /// Zapisan izbor pretvoren u jezik, ili <c>null</c> ako izbora nema
    /// odnosno ako nije jedan od ponudjenih.
    /// </summary>
    private static CultureInfo? Offered(string? name)
    {
        if (string.IsNullOrWhiteSpace(name))
        {
            return null;
        }
        foreach (string supported in Supported)
        {
            if (string.Equals(name.Trim(), supported, StringComparison.OrdinalIgnoreCase))
            {
                return string.Equals(supported, "en", StringComparison.OrdinalIgnoreCase)
                    ? CultureInfo.InvariantCulture
                    : new CultureInfo(supported);
            }
        }
        return null;
    }

    private static CultureInfo Resolve() =>
        Decide(_override, ReadInstalledChoice(), ReadPortableChoice(),
               CultureInfo.CurrentUICulture);

    /// <summary>
    /// Jezik koji nije podrzan svodi se na engleski, a ne na izuzetak.
    /// </summary>
    /// <remarks>
    /// Program koji se srusi zato sto mu je neko dao nepoznat jezik je gori od
    /// programa koji govori engleski.
    /// </remarks>
    private static CultureInfo Normalise(CultureInfo culture) =>
        IsSatellite(culture.TwoLetterISOLanguageName)
            ? new CultureInfo(culture.TwoLetterISOLanguageName)
            : CultureInfo.InvariantCulture;

    /// <summary>Ima li program satelit za ovaj jezik.</summary>
    /// <remarks>
    /// Engleski se ovde NE broji: on je neutralni resurs u samoj biblioteci, a
    /// kao izbor se svodi na <see cref="CultureInfo.InvariantCulture"/>.
    /// </remarks>
    private static bool IsSatellite(string? name)
    {
        if (string.IsNullOrWhiteSpace(name))
        {
            return false;
        }
        // "en" je neutralni resurs; kao izbor se svodi na InvariantCulture, pa
        // se ovde ne broji kao satelit.
        foreach (string supported in Supported)
        {
            if (string.Equals(name, supported, StringComparison.OrdinalIgnoreCase) &&
                !string.Equals(supported, "en", StringComparison.OrdinalIgnoreCase))
            {
                return true;
            }
        }
        return false;
    }

    /// <summary>
    /// Izbor zapisan pri instalaciji. <c>null</c> ako ga nema ili se ne moze
    /// procitati.
    /// </summary>
    /// <remarks>
    /// Nedostupan registar NIJE greska: program prekopiran bez instalacije
    /// mora raditi. Zato se svaki izuzetak ovde guta i odluka pada na sledeci
    /// korak.
    /// </remarks>
    /// <summary>
    /// Izbor iz fajla pored programa. <c>null</c> ako ga nema.
    /// </summary>
    /// <remarks>
    /// Cita se samo prvi red i samo prve dve reci - fajl koji je neko otvorio
    /// i dopisao objasnjenje ne sme oboriti program.
    /// </remarks>
    private static string? ReadPortableChoice()
    {
        try
        {
            string? directory = AppContext.BaseDirectory;
            if (string.IsNullOrEmpty(directory))
            {
                return null;
            }
            string path = Path.Combine(directory, PortableFile);
            if (!File.Exists(path))
            {
                return null;
            }
            using var reader = new StreamReader(path);
            return reader.ReadLine()?.Trim();
        }
        catch (Exception exception) when (exception is IOException
                                              or UnauthorizedAccessException
                                              or System.Security.SecurityException)
        {
            return null;
        }
    }

    private static string? ReadInstalledChoice()
    {
        if (!OperatingSystem.IsWindows())
        {
            return null;
        }
        try
        {
            using var key = Microsoft.Win32.Registry.LocalMachine.OpenSubKey(RegistryPath);
            return key?.GetValue(RegistryValue) as string;
        }
        catch (Exception exception) when (exception is UnauthorizedAccessException
                                              or System.Security.SecurityException
                                              or IOException)
        {
            return null;
        }
    }
}

/// <summary>
/// Tekst koji korisnik vidi.
/// </summary>
/// <remarks>
/// Kljuc koji ne postoji vraca sam kljuc u uglastim zagradama umesto da baci
/// izuzetak. Prazan ili srusen prozor zbog jedne nedostajuce niske je gori od
/// jedne cudne reci na ekranu - a test
/// <c>EverySerbianKeyExistsInEnglish</c> ionako drzi da se to ne desi.
/// </remarks>
public static class Strings
{
    private static readonly ResourceManager Manager =
        new("G2710.Localization.Resources.Strings", typeof(Strings).Assembly);

    public static string Get(string key)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(key);
        try
        {
            return Manager.GetString(key, Language.Current) ?? $"[{key}]";
        }
        catch (MissingManifestResourceException)
        {
            return $"[{key}]";
        }
    }

    /// <summary>Tekst sa umetnutim vrednostima.</summary>
    public static string Format(string key, params object?[] arguments) =>
        string.Format(CultureInfo.CurrentCulture, Get(key), arguments);
}
