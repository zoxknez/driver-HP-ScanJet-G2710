using System;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Text;

namespace G2710.Interop;

/// <summary>
/// Most izmedju native poziva i upravljanog koda.
/// </summary>
/// <remarks>
/// ZASTO OVAKO, A NE OBICNIM DELEGATOM
///
/// Delegat prosledjen native strani mora ostati ziv dok ga ta strana moze
/// pozvati. GC to ne zna: vidi da lokalnu promenljivu vise niko ne cita i
/// pokupi je, a native strana onda skoci na oslobodjenu memoriju. Ishod je
/// rusenje koje se desava retko, kod korisnika, i nikada u testu - jer test
/// ne stigne da izazove sakupljanje na pravom mestu.
///
/// Ovde delegata nema. Native strana dobija pokazivac na STATICKU metodu, koja
/// se ne skuplja jer nikada nije ni bila objekat. Stanje putuje kroz `user`
/// parametar kao GCHandle - koji je eksplicitno ziv, i eksplicitno se oslobadja.
///
/// Cena je jedan `unsafe` blok. Dobit je da cela klasa gresaka ne postoji.
///
/// IZUZETAK NE SME PRECI GRANICU
///
/// Callback koji baci izuzetak natrag u native kod rusi proces bez traga -
/// .NET runtime ga tamo ne moze uhvatiti. Zato je telo svakog trampolina u
/// try/catch, i izuzetak korisnikovog koda se tumaci kao zahtev za prekid.
/// </remarks>
internal static unsafe class Callbacks
{
    /// <summary>Stanje koje putuje kroz native `user` pokazivac.</summary>
    internal sealed class Context
    {
        public Func<int, bool>? Progress;
        public Action<int, string>? Log;

        /// <summary>Izuzetak iz korisnikovog koda, da se moze prijaviti posle.</summary>
        public Exception? Failure;
    }

    /// <summary>
    /// GCHandle koji zivi tacno onoliko koliko native strana moze da zove.
    /// </summary>
    /// <remarks>
    /// Kontekst se cita KROZ handle, a ne iz polja strukture.
    ///
    /// Prva verzija je drzala i `public Context Context { get; }`. Radila je -
    /// ali je GCHandle tada bio ukras: objekat je bio dostupan preko polja, pa
    /// ga GC ionako nije dirao. Mutaciona provera je to i pokazala: zamena
    /// obicnog handle-a SLABIM nije oborila nijedan test, jer je polje radilo
    /// posao umesto njega.
    ///
    /// Ovako je handle jedino sto kontekst drzi u zivotu - kao sto i treba da
    /// bude, i kao sto se sada moze izmeriti.
    /// </remarks>
    internal readonly struct Pin : IDisposable
    {
        private readonly GCHandle _handle;

        public Pin(Context context)
        {
            _handle = GCHandle.Alloc(context);
        }

        public IntPtr Pointer => GCHandle.ToIntPtr(_handle);

        /// <summary>Kontekst, ako ga handle jos drzi.</summary>
        public Context? Target => _handle.IsAllocated ? _handle.Target as Context : null;

        public void Dispose()
        {
            if (_handle.IsAllocated)
            {
                _handle.Free();
            }
        }
    }

    private static Context? Resolve(IntPtr user)
    {
        if (user == IntPtr.Zero)
        {
            return null;
        }
        var handle = GCHandle.FromIntPtr(user);
        return handle.IsAllocated ? handle.Target as Context : null;
    }

    /// <summary>Napredak. Nula znaci "prekini".</summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    internal static int Progress(int percent, IntPtr user)
    {
        Context? context = Resolve(user);
        if (context?.Progress is null)
        {
            return 1;
        }
        try
        {
            return context.Progress(percent) ? 1 : 0;
        }
        catch (Exception exception)
        {
            // Izuzetak se PAMTI pa prijavljuje sa upravljane strane. Da se
            // pusti napolje, srusio bi proces bez ijedne poruke.
            context.Failure ??= exception;
            return 0;
        }
    }

    /// <summary>Red dnevnika. Poruka je UTF-8, NUL-terminisana.</summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    internal static void Log(int level, IntPtr message, IntPtr user)
    {
        Context? context = Resolve(user);
        if (context?.Log is null)
        {
            return;
        }
        try
        {
            context.Log(level, Utf8.ToString(message));
        }
        catch (Exception exception)
        {
            context.Failure ??= exception;
        }
    }
}

/// <summary>Citanje UTF-8 niski koje daje native strana.</summary>
internal static class Utf8
{
    /// <summary>NUL-terminisana niska sa native strane. NULL daje prazan string.</summary>
    internal static string ToString(IntPtr pointer) =>
        pointer == IntPtr.Zero ? string.Empty : (Marshal.PtrToStringUTF8(pointer) ?? string.Empty);

    /// <summary>
    /// Procitaj nisku kroz ugovor "bafer daje pozivalac".
    /// </summary>
    /// <remarks>
    /// Native strana upisuje najvise `capacity` bajtova UKLJUCUJUCI zavrsnu
    /// nulu, a vraca koliko je bajtova POTREBNO bez nje. Zato se prvo pita
    /// koliko treba, pa se alocira tacno toliko - umesto da se pogadja nekim
    /// "dovoljno velikim" brojem i tiho gubi kraj poruke.
    /// </remarks>
    internal static string Read(Func<byte[], int, int> reader)
    {
        int needed = reader(Array.Empty<byte>(), 0);
        if (needed <= 0)
        {
            return string.Empty;
        }

        var buffer = new byte[needed + 1];
        int again = reader(buffer, buffer.Length);

        // Ako je poruka u medjuvremenu porasla, uzima se ono sto je stalo.
        // Ponavljati u petlji nema smisla: poruka se menja samo kada se desi
        // nova greska, a tada je i stara nebitna.
        int length = Math.Min(again, needed);
        return Encoding.UTF8.GetString(buffer, 0, Math.Max(0, length));
    }
}
