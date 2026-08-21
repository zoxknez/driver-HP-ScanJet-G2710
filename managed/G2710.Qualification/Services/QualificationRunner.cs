using System;
using System.Diagnostics;
using System.IO;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using G2710.Qualification.Models;

namespace G2710.Qualification.Services;

/// <summary>Zasto pokretanje nije uspelo.</summary>
public enum RunFailure
{
    None,
    ToolMissing,
    ToolCrashed,
    NoReport,
    Cancelled,
    DeviceNotFound,
    DeviceBusy,
}

public sealed record RunOutcome(
    RunFailure Failure,
    QualificationReport? Report,
    string ToolOutput,
    int ExitCode)
{
    public bool Succeeded => Failure == RunFailure.None && Report is not null;
}

/// <summary>
/// Pokrece g2710ctl qualify i cita izvestaj.
/// </summary>
/// <remarks>
/// Wizard NAMERNO ne prica sa uredjajem sam. Alat je poseban proces, pa
/// zaglavljen ili srusen transport ne obara prozor pred korisnikom - wizard
/// preostaje da to prijavi i ponudi ponovni pokusaj. Za alat koji se koristi
/// na daljinu to je vaznije od ustede jednog sloja.
/// </remarks>
public class QualificationRunner
{
    private readonly string _toolPath;
    private readonly string _workingDirectory;

    public QualificationRunner(string toolPath, string? workingDirectory = null)
    {
        _toolPath = toolPath;
        _workingDirectory = workingDirectory ?? Path.GetDirectoryName(toolPath) ?? ".";
    }

    /// <summary>Gde se upisuje izvestaj.</summary>
    public string ReportPath => Path.Combine(_workingDirectory, "test-results.json");

    /// <summary>
    /// Pokreni kvalifikaciju.
    /// </summary>
    /// <param name="transport">"usbscan" u radu, "sim" za probu bez uredjaja.</param>
    /// <param name="safetyLevel">Trazeni nivo; plafon build-a ga i dalje ogranicava.</param>
    /// <param name="onOutput">Svaki red ispisa, dok stize.</param>
    /// <remarks>
    /// Virtuelan da bi se tok wizarda mogao testirati bez pokretanja procesa.
    /// Bez toga bi testovi zvali osnovnu implementaciju i ne bi merili nista -
    /// `new` metod u izvedenom razredu se ne poziva kroz osnovni tip.
    /// </remarks>
    public virtual async Task<RunOutcome> RunAsync(
        string transport,
        int safetyLevel,
        Action<string>? onOutput,
        CancellationToken token)
    {
        if (!File.Exists(_toolPath))
        {
            return new RunOutcome(RunFailure.ToolMissing, null, string.Empty, -1);
        }

        // Stari izvestaj se brise PRE pokretanja. Bez toga bi neuspeo prolaz
        // pokazao proslu, uspesnu proveru - najgori moguci ishod.
        TryDelete(ReportPath);

        var arguments =
            $"qualify --transport {transport} --safety-level {safetyLevel} " +
            $"--out \"{ReportPath}\"";

        var startInfo = new ProcessStartInfo
        {
            FileName = _toolPath,
            Arguments = arguments,
            WorkingDirectory = _workingDirectory,
            RedirectStandardOutput = true,
            RedirectStandardError = true,
            UseShellExecute = false,
            CreateNoWindow = true,
            StandardOutputEncoding = Encoding.UTF8,
            StandardErrorEncoding = Encoding.UTF8,
        };

        var output = new StringBuilder();
        using var process = new Process { StartInfo = startInfo, EnableRaisingEvents = true };

        void Collect(object _, DataReceivedEventArgs args)
        {
            if (args.Data is null)
            {
                return;
            }
            lock (output)
            {
                output.AppendLine(args.Data);
            }
            onOutput?.Invoke(args.Data);
        }

        process.OutputDataReceived += Collect;
        process.ErrorDataReceived += Collect;

        try
        {
            process.Start();
        }
        catch (Exception exception) when (exception is System.ComponentModel.Win32Exception
                                              or InvalidOperationException)
        {
            return new RunOutcome(RunFailure.ToolCrashed, null, exception.Message, -1);
        }

        process.BeginOutputReadLine();
        process.BeginErrorReadLine();

        try
        {
            await process.WaitForExitAsync(token).ConfigureAwait(false);
        }
        catch (OperationCanceledException)
        {
            KillQuietly(process);
            return new RunOutcome(RunFailure.Cancelled, null, output.ToString(), -1);
        }

        var text = output.ToString();
        var exitCode = process.ExitCode;

        // Izvestaj se cita i kada je izlazni kod razlicit od nule - tada je
        // najpotrebniji, jer nosi KOJA provera je pala.
        var report = File.Exists(ReportPath)
            ? QualificationReport.TryParse(await File.ReadAllTextAsync(ReportPath, token)
                                               .ConfigureAwait(false))
            : null;

        if (report is not null)
        {
            return new RunOutcome(RunFailure.None, report, text, exitCode);
        }

        return new RunOutcome(ClassifyExit(exitCode), null, text, exitCode);
    }

    /// <summary>
    /// Prevedi izlazni kod alata u razlog. Kodovi su iz native/cli/main.cpp.
    /// </summary>
    internal static RunFailure ClassifyExit(int exitCode) => exitCode switch
    {
        3 => RunFailure.DeviceNotFound,   // otvaranje uredjaja
        6 => RunFailure.DeviceNotFound,   // na portu je tudji uredjaj
        7 => RunFailure.DeviceBusy,       // drzi ga drugi klijent
        _ => RunFailure.NoReport,
    };

    private static void TryDelete(string path)
    {
        try
        {
            if (File.Exists(path))
            {
                File.Delete(path);
            }
        }
        catch (IOException)
        {
            // Fajl drzi neko drugi. Prolaz svejedno tece; ako izvestaj ne bude
            // prepisan, TryParse ce vratiti stari i to je vidljivo po datumu.
        }
        catch (UnauthorizedAccessException)
        {
        }
    }

    private static void KillQuietly(Process process)
    {
        try
        {
            if (!process.HasExited)
            {
                process.Kill(entireProcessTree: true);
            }
        }
        catch (Exception exception) when (exception is InvalidOperationException
                                              or System.ComponentModel.Win32Exception)
        {
            // Proces je vec otisao. Nema sta da se prijavi.
        }
    }
}
