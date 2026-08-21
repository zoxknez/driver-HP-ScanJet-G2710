using System;
using System.Collections.Generic;
using System.IO;

namespace G2710.Qualification.Services;

/// <summary>
/// Nalazi g2710ctl.exe.
/// </summary>
/// <remarks>
/// Prijatelj raspakuje ZIP i klikne wizard. Ako alat nije pored njega, jedina
/// upotrebljiva poruka je GDE je trazen - "nije pronadjen" ne pomaze nikome.
/// Zato ovaj razred vraca i spisak pretrazenih mesta.
/// </remarks>
public static class ToolLocator
{
    public const string ToolFileName = "g2710ctl.exe";

    public sealed record Result(string? Path, IReadOnlyList<string> SearchedIn)
    {
        public bool Found => Path is not null;
    }

    /// <summary>
    /// Pretrazi uobicajena mesta, redom od najverovatnijeg.
    /// </summary>
    /// <param name="baseDirectory">
    /// Direktorijum wizarda. Izdvojen kao parametar da bi se pretraga mogla
    /// testirati bez instalacije.
    /// </param>
    public static Result Locate(string? baseDirectory = null)
    {
        var root = baseDirectory ?? AppDirectory();
        var candidates = new List<string>
        {
            // 1. Pored wizarda - tako izgleda isporuceni ZIP.
            Path.Combine(root, ToolFileName),

            // 2. U podfolderu, ako je paket slozen po komponentama.
            Path.Combine(root, "tools", ToolFileName),

            // 3. Razvojni build, kada se wizard pokrece iz Visual Studia.
            Path.Combine(root, "..", "..", "..", "..", "..", "build", "native", "cli",
                         "Release", ToolFileName),
        };

        var searched = new List<string>();
        foreach (var candidate in candidates)
        {
            var full = Path.GetFullPath(candidate);
            searched.Add(full);

            if (File.Exists(full))
            {
                return new Result(full, searched);
            }
        }
        return new Result(null, searched);
    }

    private static string AppDirectory()
    {
        // AppContext.BaseDirectory, ne Assembly.Location: wizard se objavljuje
        // kao JEDAN fajl, a tamo Location vraca prazan string. Kompajler to i
        // prijavljuje kao IL3000.
        var baseDirectory = AppContext.BaseDirectory;
        return string.IsNullOrEmpty(baseDirectory)
            ? Directory.GetCurrentDirectory()
            : baseDirectory;
    }
}
