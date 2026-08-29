using System;
using System.Collections.Generic;
using G2710.App;
using G2710.Interop;
using Xunit;

namespace G2710.App.Tests;

/// <summary>
/// <see cref="ScanWorkflow.Run"/> - deo aplikacije koji najvise nosi.
/// </summary>
/// <remarks>
/// Obecava dve stvari koje se ne smeju pogresiti:
///
///   otkazan prolaz NIKADA ne vraca delimicnu sliku
///   prolaz se zatvara i kada red ili callback padne
///
/// Prva verzija je imala samo jedan test koji provozi ceo tok nad simulatorom i
/// proveri da slika izadje. To ne meri nijedno od ta dva obecanja.
///
/// Ovde nema laznjaka: Scanner je zapecacen i ima privatni konstruktor, pa se
/// vozi pravi ABI nad simulatorom. Otkazivanje se izaziva IZ progress
/// callback-a - deterministicno, bez spavanja i bez trke.
/// </remarks>
public sealed class ScanWorkflowTests
{
    private static Scanner Open()
    {
        Scanner scanner = Scanner.Open(new ScannerOptions
        {
            Transport = ScannerTransport.Simulator,
            RequestedSafetyLevel = 5,
            ClientName = "workflow-test",
        });
        scanner.Identify();
        scanner.Begin();
        scanner.Warmup(TimeSpan.Zero);
        return scanner;
    }

    private static ScanSettings Small(int height = 12) => new()
    {
        Resolution = 300,
        ColorMode = ScanColorMode.Color,
        BitsPerChannel = 8,
        Width = 32,
        Height = height,
        AllowUnqualified = true,
    };

    [Fact]
    public void A_complete_pass_gives_an_image_of_the_planned_size()
    {
        using Scanner scanner = Open();
        ScanImage? image = ScanWorkflow.Run(scanner, Small());

        Assert.NotNull(image);
        Assert.Equal(32, image!.Width);
        Assert.True(image.Height > 0);
        Assert.Equal(image.Stride * image.Height, image.Pixels.Length);
    }

    [Fact]
    public void Progress_climbs_to_a_hundred_and_never_goes_back()
    {
        using Scanner scanner = Open();
        var seen = new List<int>();

        ScanImage? image = ScanWorkflow.Run(scanner, Small(), seen.Add);

        Assert.NotNull(image);
        Assert.NotEmpty(seen);
        Assert.Equal(100, seen[^1]);
        for (int i = 1; i < seen.Count; ++i)
        {
            Assert.True(seen[i] >= seen[i - 1], "napredak je isao unazad");
        }
    }

    [Fact]
    public void Cancelling_mid_pass_gives_null_and_never_half_an_image()
    {
        using Scanner scanner = Open();

        // Otkazivanje IZ callback-a: `Run` ga zove posle svakog reda, pa je
        // trenutak prekida tacno odredjen - bez spavanja i bez trke.
        int lines = 0;
        ScanImage? image = ScanWorkflow.Run(scanner, Small(), _ =>
        {
            if (++lines == 3)
            {
                scanner.Cancel();
            }
        });

        // Delimicna slika je gora od nikakve: izgleda kao ispravan rezultat.
        Assert.Null(image);
        Assert.Equal(3, lines);
    }

    [Fact]
    public void The_device_is_usable_again_after_a_cancelled_pass()
    {
        using Scanner scanner = Open();

        int lines = 0;
        Assert.Null(ScanWorkflow.Run(scanner, Small(), _ =>
        {
            if (++lines == 2) { scanner.Cancel(); }
        }));

        // Da prolaz nije zatvoren, cip bi i dalje skenirao i sledeci prolaz bi
        // pao - na tudjem racunaru kao "skener se zaglavio posle Prekini".
        ScanImage? second = ScanWorkflow.Run(scanner, Small());
        Assert.NotNull(second);
    }

    [Fact]
    public void A_throwing_callback_still_closes_the_pass()
    {
        using Scanner scanner = Open();

        // Greska u korisnikovom kodu ne sme ostaviti cip da skenira. ScanEnd je
        // u `finally` bas zbog ovoga.
        Assert.Throws<InvalidOperationException>(() =>
            ScanWorkflow.Run(scanner, Small(), _ => throw new InvalidOperationException("iz UI-ja")));

        ScanImage? after = ScanWorkflow.Run(scanner, Small());
        Assert.NotNull(after);
    }

    [Fact]
    public void An_impossible_request_never_reaches_the_capture()
    {
        using Scanner scanner = Open();

        // Plan pada pre nego sto se ista pomeri; `Run` to ne sme pretvoriti u
        // praznu sliku.
        Assert.Throws<ScannerException>(() =>
            ScanWorkflow.Run(scanner, Small() with { BitsPerChannel = 12 }));

        ScanImage? after = ScanWorkflow.Run(scanner, Small());
        Assert.NotNull(after);
    }

    [Fact]
    public void Null_arguments_are_refused_before_anything_moves()
    {
        using Scanner scanner = Open();
        Assert.Throws<ArgumentNullException>(() => ScanWorkflow.Run(null!, Small()));
        Assert.Throws<ArgumentNullException>(() => ScanWorkflow.Run(scanner, null!));
    }
}
