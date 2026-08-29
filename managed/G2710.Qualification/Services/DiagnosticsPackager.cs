using System;
using System.Diagnostics;
using System.IO;
using System.Linq;
using G2710.Localization;

namespace G2710.Qualification.Services;

/// <summary>
/// Pokrece collect-diagnostics.ps1 i vraca putanju do ZIP-a.
/// </summary>
/// <remarks>
/// Zasto uopste iz wizarda, kad skript stoji u istom folderu: zato sto je
/// "desni klik na .ps1 pa Run with PowerShell" najslabija karika celog lanca.
/// Covek koji testira skener ne mora da zna sta je PowerShell, a ako taj korak
/// preskoci, nazad stize samo JSON bez ijednog podatka o masini - i onda se
/// H1-A ne moze oceniti.
///
/// Skript ostaje u paketu i moze se pokrenuti rucno. Ovo je dodatni put, ne
/// zamena: ako pukne, izvestaj je vec sacuvan i wizard to i kaze.
/// </remarks>
public static class DiagnosticsPackager
{
    public const string ScriptName = "collect-diagnostics.ps1";

    public sealed record Result(string? ZipPath, string? Error)
    {
        public bool Ok => ZipPath is not null;
    }

    /// <summary>Skript se trazi pored samog programa, kao i g2710ctl.</summary>
    public static string? FindScript()
    {
        var path = Path.Combine(AppContext.BaseDirectory, ScriptName);
        return File.Exists(path) ? path : null;
    }

    /// <summary>
    /// Napravi ZIP u <paramref name="outputDirectory"/>. Vraca gresku umesto
    /// izuzetka - ovo se poziva posle uspesnog snimanja izvestaja i ne sme da
    /// obori ono sto je vec uspelo.
    /// </summary>
    public static Result Run(string reportPath, string outputDirectory,
                             TimeSpan timeout)
    {
        var script = FindScript();
        if (script is null)
        {
            return new Result(null, Strings.Format("Wiz_Zip_NoScript", ScriptName));
        }

        // Snimak stanja PRE pokretanja. ZIP se prepoznaje kao onaj koji ranije
        // nije postojao; oslanjanje na "najnoviji fajl" bi pokupilo tudji ZIP
        // od proslog puta ako skript padne pre nego sto stigne da napravi svoj.
        var before = SafeListZips(outputDirectory);

        try
        {
            // -ExecutionPolicy Bypass jer je podrazumevana politika na kucnom
            // Windowsu Restricted - skript se inace ne bi ni pokrenuo. Vazi
            // samo za ovaj proces; postavka racunara se ne dira.
            var info = new ProcessStartInfo
            {
                FileName = "powershell.exe",
                UseShellExecute = false,
                CreateNoWindow = true,
                RedirectStandardOutput = true,
                RedirectStandardError = true,
            };
            info.ArgumentList.Add("-NoProfile");
            info.ArgumentList.Add("-NonInteractive");
            info.ArgumentList.Add("-ExecutionPolicy");
            info.ArgumentList.Add("Bypass");
            info.ArgumentList.Add("-File");
            info.ArgumentList.Add(script);
            info.ArgumentList.Add("-OutputDirectory");
            info.ArgumentList.Add(outputDirectory);
            info.ArgumentList.Add("-ReportPath");
            info.ArgumentList.Add(reportPath);

            using var process = Process.Start(info);
            if (process is null)
            {
                return new Result(null, Strings.Get("Wiz_Zip_NoPowerShell"));
            }

            // Citanje mora ici PRE WaitForExit: cev ima ogranicen bafer, a
            // pnputil ume da ispise dvadesetak kilobajta. Pun bafer bi zaustavio
            // skript, a mi bismo cekali proces koji ceka nas.
            var stdout = process.StandardOutput.ReadToEnd();
            var stderr = process.StandardError.ReadToEnd();

            if (!process.WaitForExit((int)timeout.TotalMilliseconds))
            {
                TryKill(process);
                return new Result(null, "Sakupljanje podataka je trajalo predugo.");
            }

            if (process.ExitCode != 0)
            {
                var detail = string.IsNullOrWhiteSpace(stderr) ? stdout : stderr;
                return new Result(null, Trim(detail, process.ExitCode));
            }

            var created = SafeListZips(outputDirectory).Except(before).ToList();
            if (created.Count == 0)
            {
                return new Result(null, Strings.Get("Wiz_Zip_NoFile"));
            }

            return new Result(created.OrderBy(p => p).Last(), null);
        }
        catch (Exception exception) when (exception is System.ComponentModel.Win32Exception
                                              or InvalidOperationException
                                              or IOException)
        {
            return new Result(null, exception.Message);
        }
    }

    private static string[] SafeListZips(string directory)
    {
        try
        {
            return Directory.Exists(directory)
                ? Directory.GetFiles(directory, "G2710-HardwareReport-*.zip")
                : Array.Empty<string>();
        }
        catch (Exception exception) when (exception is IOException
                                              or UnauthorizedAccessException)
        {
            return Array.Empty<string>();
        }
    }

    private static void TryKill(Process process)
    {
        try
        {
            process.Kill(entireProcessTree: true);
        }
        catch (Exception exception) when (exception is InvalidOperationException
                                              or System.ComponentModel.Win32Exception
                                              or NotSupportedException)
        {
            // Proces je vec otisao. Nema sta da se prijavi.
        }
    }

    private static string Trim(string text, int exitCode)
    {
        var line = text.Split('\n')
                       .Select(l => l.Trim())
                       .LastOrDefault(l => l.Length > 0);

        return string.IsNullOrEmpty(line)
            ? $"PowerShell je vratio {exitCode}."
            : line;
    }
}
