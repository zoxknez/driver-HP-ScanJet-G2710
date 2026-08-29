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

    // --- format samog fajla ---------------------------------------------
    //
    // PDF writer je nas, bez spoljne biblioteke. Ono sto se u njemu moze
    // pogresiti ne vidi se u broju strana nego u bajtovima - i ne kod nas nego
    // u citacu koji je stroziji od onog kojim smo probali.

    [Fact]
    public void The_binary_marker_really_contains_high_bytes()
    {
        // PDF zaglavlje nosi komentar sa bajtovima iznad 0x7F. On postoji da bi
        // alati znali da je fajl BINARAN; ako se zapise kroz ASCII enkoder,
        // svaki takav bajt postane '?' i marker vise ne znaci nista.
        string path = TempPdf();
        try
        {
            ImageExport.Save(Page(0x40), ExportFormat.Pdf, path);
            byte[] bytes = File.ReadAllBytes(path);

            Assert.Equal((byte)'%', bytes[0]);

            // Drugi red pocinje sa '%' pa slede binarni bajtovi.
            int marker = Array.IndexOf(bytes, (byte)'\n') + 1;
            Assert.Equal((byte)'%', bytes[marker]);

            byte[] high = bytes[(marker + 1)..(marker + 5)];
            Assert.All(high, b => Assert.True(b > 0x7F,
                $"binarni marker sadrzi 0x{b:X2} umesto bajta iznad 0x7F"));
        }
        finally
        {
            File.Delete(path);
        }
    }

    [Fact]
    public void FlateDecode_streams_are_really_zlib()
    {
        // PDF /FlateDecode je ZLIB (RFC 1950) - dva bajta zaglavlja i Adler-32
        // na kraju - a ne goli deflate (RFC 1951). Blagi citaci progledaju kroz
        // prste; stroziji odbiju sliku. To je otkaz koji se vidi tek na tudjem
        // racunaru, sa drugim citacem.
        string path = TempPdf();
        try
        {
            ImageExport.Save(Page(0x40), ExportFormat.Pdf, path);
            byte[] bytes = File.ReadAllBytes(path);
            string text = Encoding.Latin1.GetString(bytes);

            int dictionary = text.IndexOf("/FlateDecode", StringComparison.Ordinal);
            Assert.True(dictionary > 0, "nema FlateDecode toka");

            int start = text.IndexOf("stream\n", dictionary, StringComparison.Ordinal)
                        + "stream\n".Length;

            // Prvi bajt zlib zaglavlja je CMF; za deflate sa prozorom 32K to je
            // 0x78. Drugi je FLG, i (CMF*256 + FLG) mora biti deljivo sa 31.
            byte cmf = bytes[start];
            byte flg = bytes[start + 1];

            Assert.Equal(0x78, cmf);
            Assert.Equal(0, (cmf * 256 + flg) % 31);
        }
        finally
        {
            File.Delete(path);
        }
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

    // Oba jezika, jer je poenta broj - a broj se u prevodu izgubi lakse nego
    // recenica. Engleski je prvi zato sto je on ono sto vidi neko ko nista nije
    // birao pri instalaciji.
    [Theory]
    [InlineData("en", "No pages set aside", "will give 2 pages", "will give 4 pages")]
    [InlineData("sr", "Nema odloženih", "2 strane", "4 strana")]
    public void The_summary_says_how_many_pages_the_pdf_will_have(string language,
                                                                  string empty,
                                                                  string afterOne,
                                                                  string afterThree)
    {
        // Broj se ne sme pogadjati: korisnik koji je odlozio tri stranice mora
        // videti da ce PDF imati cetiri.
        using var _ = new LanguageScope(language);

        var model = new MainViewModel();
        Assert.Contains(empty, model.PagesSummary);

        model.SetLastImageForTest(Page(0x11));
        model.AddPage();
        Assert.Equal(1, model.PageCount);
        Assert.Contains(afterOne, model.PagesSummary);

        model.AddPage();
        model.AddPage();
        Assert.Equal(3, model.PageCount);
        Assert.Contains(afterThree, model.PagesSummary);
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
