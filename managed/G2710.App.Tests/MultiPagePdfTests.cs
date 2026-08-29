using System;
using System.Collections.Generic;
using System.IO;
using System.Text;
using G2710.App;
using Xunit;

namespace G2710.App.Tests;

/// <summary>
/// Visestranicni PDF, od odlaganja stranice do fajla.
/// </summary>
/// <remarks>
/// Izvozni sloj je od pocetka primao vise stranica, ali kroz UI nije bilo
/// nijednog puta da se druga stranica doda - pa je mogucnost postojala samo u
/// kodu. Ovi testovi drze taj put otvorenim.
/// </remarks>
public sealed class MultiPagePdfTests
{
    private static ScanImage Page(byte fill, int width = 4, int height = 3)
    {
        var pixels = new byte[width * height * 3];
        Array.Fill(pixels, fill);
        return new ScanImage(width, height, 8, 3, pixels);
    }

    private static string TempPdf() =>
        Path.Combine(Path.GetTempPath(), $"g2710-pages-{Guid.NewGuid():N}.pdf");

    // Broj strana se cita iz samog fajla, a ne iz onoga sto je kod nameravao.
    private static int CountPdfPages(string path)
    {
        string text = Encoding.Latin1.GetString(File.ReadAllBytes(path));
        int count = 0;
        int at = 0;
        while ((at = text.IndexOf("/Type /Page", at, StringComparison.Ordinal)) >= 0)
        {
            // "/Type /Pages" je stablo, ne strana.
            if (!text.AsSpan(at).StartsWith("/Type /Pages"))
            {
                ++count;
            }
            at += "/Type /Page".Length;
        }
        return count;
    }

    [Fact]
    public void One_image_gives_a_single_page_document()
    {
        string path = TempPdf();
        try
        {
            ImageExport.Save(Page(0x40), ExportFormat.Pdf, path);
            Assert.Equal(1, CountPdfPages(path));
        }
        finally
        {
            File.Delete(path);
        }
    }

    [Fact]
    public void Extra_pages_really_reach_the_file()
    {
        string path = TempPdf();
        try
        {
            var extra = new List<ScanImage> { Page(0x80), Page(0xC0) };
            ImageExport.Save(Page(0x40), ExportFormat.Pdf, path, extra);

            // Tri, ne jedna: prva slika plus dve odlozene.
            Assert.Equal(3, CountPdfPages(path));
        }
        finally
        {
            File.Delete(path);
        }
    }

    [Fact]
    public void Pages_of_different_sizes_are_accepted()
    {
        // Stranice skenirane u razlicitim rezolucijama imaju razlicite
        // dimenzije. PDF to podnosi; writer ne sme da pretpostavi jednu velicinu.
        string path = TempPdf();
        try
        {
            var extra = new List<ScanImage> { Page(0x80, width: 8, height: 6) };
            ImageExport.Save(Page(0x40, width: 4, height: 3), ExportFormat.Pdf, path, extra);
            Assert.Equal(2, CountPdfPages(path));
        }
        finally
        {
            File.Delete(path);
        }
    }

    [Fact]
    public void Only_pdf_carries_more_than_one_page()
    {
        // Tih gubitak stranica je gori od odbijenog izvoza: korisnik bi dobio
        // fajl koji izgleda ispravno a nosi samo jednu sliku.
        string path = Path.Combine(Path.GetTempPath(), $"g2710-{Guid.NewGuid():N}.png");
        var extra = new List<ScanImage> { Page(0x80) };

        Assert.Throws<ArgumentException>(() =>
            ImageExport.Save(Page(0x40), ExportFormat.Png, path, extra));

        Assert.False(File.Exists(path), "fajl je napravljen iako je izvoz odbijen");
    }

    // --- put kroz aplikaciju --------------------------------------------

    [Fact]
    public void Nothing_can_be_deferred_before_anything_is_scanned()
    {
        var model = new MainViewModel();

        Assert.Equal(0, model.PageCount);
        Assert.False(model.HasPages);
        Assert.False(model.AddPageCommand.CanExecute(null));
        Assert.False(model.ClearPagesCommand.CanExecute(null));
    }

    [Fact]
    public void The_summary_says_how_many_pages_the_pdf_will_have()
    {
        // Broj se ne sme pogadjati: korisnik koji je odlozio tri stranice mora
        // videti da ce PDF imati cetiri.
        var model = new MainViewModel();
        Assert.Contains("Nema odloženih", model.PagesSummary);

        model.SetLastImageForTest(Page(0x11));
        model.AddPage();
        Assert.Equal(1, model.PageCount);
        Assert.Contains("2 strane", model.PagesSummary);

        model.AddPage();
        model.AddPage();
        Assert.Equal(3, model.PageCount);
        Assert.Contains("4 strana", model.PagesSummary);
    }

    [Fact]
    public void Clearing_pages_really_clears_them()
    {
        var model = new MainViewModel();
        model.SetLastImageForTest(Page(0x11));
        model.AddPage();
        Assert.True(model.HasPages);

        model.ClearPages();

        Assert.Equal(0, model.PageCount);
        Assert.False(model.HasPages);
        Assert.False(model.ClearPagesCommand.CanExecute(null));
    }
}
