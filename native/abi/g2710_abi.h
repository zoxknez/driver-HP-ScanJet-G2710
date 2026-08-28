/*
 * G2710.Native - stabilna C granica preko koje .NET prica sa jezgrom.
 *
 * Ovo zaglavlje je UGOVOR. Menja se namerno, nikada usput: sa druge strane
 * stoji P/Invoke koji se ne prevodi zajedno sa ovim kodom, pa promena koja
 * ovde prodje kao bezopasna tamo postaje pad aplikacije kod korisnika. Zato
 * postoji tests/unit/abi_stability_test.cpp, koji poredi izvezene simbole i
 * velicine struktura sa zapamcenom slikom.
 *
 * ----------------------------------------------------------------------------
 * ODLUKE, i zasto bas te
 * ----------------------------------------------------------------------------
 *
 * 1. GRESKA JE POVRATNA VREDNOST, ne izuzetak.
 *
 *    Nijedan C++ izuzetak ne sme preci ovu granicu - .NET runtime ga ne moze
 *    uhvatiti, a proces se rusi bez traga. Svaka funkcija vraca g2710_status,
 *    a detalj se cita kroz g2710_last_error(). Detalj je vezan za HANDLE, ne
 *    za nit i ne za proces: dva skenera u istoj aplikaciji ne smeju gaziti
 *    jedan drugom poruku.
 *
 * 2. MEMORIJU DAJE POZIVALAC.
 *
 *    ABI ne alocira nista sto .NET treba da oslobodi. Svaka funkcija koja
 *    vraca podatke prima bafer i njegovu velicinu, a vraca koliko je zaista
 *    upisano. Preterano mali bafer je G2710_STATUS_INVALID_ARGUMENT, ne tiho
 *    skracivanje - skracena slika izgleda kao pokvaren skener.
 *
 *    Jedini izuzetak je sam handle, koji se oslobadja kroz g2710_close.
 *
 * 3. CALLBACK-OVI STIZU SA RADNE NITI.
 *
 *    g2710_scan_read_line se izvrsava na niti pozivaoca, ali progress i log
 *    mogu stici i iz operacije koja traje. Pozivalac je duzan da ih prosledi
 *    svojoj UI niti sam. Ovo je zapisano ovde jer je greska koja se ne vidi u
 *    testu, nego u nasumicnom rusenju WPF prozora.
 *
 *    Callback koji baci izuzetak preko granice je greska POZIVAOCA i ponasanje
 *    je nedefinisano; .NET strana mora imati try/catch unutar delegata.
 *
 * 4. JEDAN HANDLE = JEDAN POZIVALAC.
 *
 *    Handle nije thread-safe. Paralelna upotreba istog handle-a iz dve niti je
 *    greska pozivaoca i vraca G2710_STATUS_INVALID_STATE kad se primeti - ali
 *    se ne garantuje da ce se primetiti uvek. Jedini izuzetak je g2710_cancel,
 *    koji SME iz bilo koje niti; to je i njegova svrha.
 *
 * ----------------------------------------------------------------------------
 * Licenca: GPL-2.0-or-later. Vidi LICENSE i NOTICE-hp3900.md.
 */

#ifndef G2710_ABI_H
#define G2710_ABI_H

#include <stddef.h>
#include <stdint.h>

/* G2710_ABI_STATIC: isti kod se gradi i kao DLL i kao staticka biblioteka.
 * Testovi koriste staticku da bi simulator i TransportProvider ziveli u istom
 * procesu; bez ovog prekidaca bi zaglavlje trazilo __imp_ simbole kojih tamo
 * nema. */
#if defined(_WIN32) && !defined(G2710_ABI_STATIC)
#  if defined(G2710_ABI_BUILDING)
#    define G2710_API __declspec(dllexport)
#  else
#    define G2710_API __declspec(dllimport)
#  endif
#  define G2710_CALL __cdecl
#elif defined(_WIN32)
#  define G2710_API
#  define G2710_CALL __cdecl
#else
#  define G2710_API
#  define G2710_CALL
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------------------------------------------------------------------
 * Verzija ugovora
 *
 * Menja se RUCNO, i to je poenta. .NET strana proverava g2710_abi_version()
 * pri ucitavanju; nepoklapanje je jasna poruka umesto rusenja na prvom pozivu
 * sa promenjenim potpisom.
 * ------------------------------------------------------------------------- */

#define G2710_ABI_VERSION_MAJOR 1
#define G2710_ABI_VERSION_MINOR 1

/* ---------------------------------------------------------------------------
 * Ishodi
 *
 * Brojevi su DEO UGOVORA i ne smeju se preurediti - .NET ih poredi kao
 * konstante. Novi kod se dodaje na kraj.
 * ------------------------------------------------------------------------- */

typedef enum g2710_status {
    G2710_STATUS_OK = 0,

    G2710_STATUS_NOT_OPEN = 1,
    G2710_STATUS_TIMEOUT = 2,
    G2710_STATUS_SHORT_TRANSFER = 3,
    G2710_STATUS_STALLED = 4,
    G2710_STATUS_CANCELLED = 5,

    /* Veza je nestala USRED operacije. Pozicija glave je od tog trenutka
     * NEPOZNATA i g2710_home je obavezan pre bilo cega drugog. */
    G2710_STATUS_TRANSPORT_LOST = 6,

    G2710_STATUS_DEVICE_NOT_FOUND = 7,
    G2710_STATUS_DEVICE_ERROR = 8,

    /* Uredjaj drzi drugi klijent. g2710_current_owner kaze koji. */
    G2710_STATUS_BUSY = 9,

    /* Operacija je iznad efektivnog nivoa bezbednosti. Tvrda greska, ne
     * upozorenje - i ne popravlja se ponovnim pokusajem. */
    G2710_STATUS_SAFETY_VIOLATION = 10,

    G2710_STATUS_NOT_IMPLEMENTED = 11,
    G2710_STATUS_INVALID_ARGUMENT = 12,
    G2710_STATUS_INVALID_STATE = 13,
    G2710_STATUS_INTERNAL = 14
} g2710_status;

/* Kratko ime ishoda, na engleskom, za log. Nikada NULL. Pokazivac je na
 * staticki literal i vazi zauvek. */
G2710_API const char* G2710_CALL g2710_status_name(g2710_status status);

/* ---------------------------------------------------------------------------
 * Stanje uredjaja
 * ------------------------------------------------------------------------- */

typedef enum g2710_device_state {
    G2710_STATE_DISCONNECTED = 0,
    G2710_STATE_OPENED = 1,
    G2710_STATE_IDENTIFIED = 2,
    G2710_STATE_IDLE = 3,
    G2710_STATE_WARMING_UP = 4,
    G2710_STATE_HOMING = 5,
    G2710_STATE_CALIBRATING = 6,
    G2710_STATE_SCANNING = 7,
    G2710_STATE_CANCELLING = 8,
    G2710_STATE_TRANSPORT_LOST = 9,
    G2710_STATE_FAULTED = 10,
    G2710_STATE_EMERGENCY_STOPPED = 11
} g2710_device_state;

typedef enum g2710_color_mode {
    G2710_COLOR = 0,
    G2710_GRAY = 1,
    G2710_LINEART = 2
} g2710_color_mode;

typedef enum g2710_transport {
    /* \\.\Usbscan0 - pravi uredjaj. */
    G2710_TRANSPORT_USBSCAN = 0,

    /* Simulator. Postoji u ABI-ju namerno: aplikacija se mora moci voziti
     * cela, bez skenera, i to je jedini nacin da GUI ima sta da pokaze pre
     * nego sto hardver stigne. */
    G2710_TRANSPORT_SIM = 1
} g2710_transport;

/* ---------------------------------------------------------------------------
 * Handle
 * ------------------------------------------------------------------------- */

typedef struct g2710_device g2710_device;

/* Podesavanja otvaranja.
 *
 * Struktura nosi svoju velicinu kao PRVO polje. Tako .NET strana izgradjena
 * uz stariju verziju ostaje upotrebljiva: ABI vidi manji `size` i zna da
 * novija polja nisu popunjena. Bez toga bi svako dodato polje bilo prelomna
 * promena. */
typedef struct g2710_open_options {
    uint32_t size;

    g2710_transport transport;

    /* 1..5. Efektivni nivo je min(BuildSafetyCeiling, ovo) - plafon build-a
     * se ovim NE MOZE podici. */
    int32_t requested_safety_level;

    /* Ime koje vidi sledeci klijent kada zatekne zauzet uredjaj. UTF-8,
     * NUL-terminisano. NULL znaci "g2710". */
    const char* client_name;

    /* Koliko cekati na ekskluzivnu sesiju, u milisekundama. 0 = podrazumevano
     * (5000). */
    uint32_t acquire_timeout_ms;

    /* Ako != 0, svaki transfer se pamti i moze se ispisati kroz
     * g2710_write_trace.
     *
     * Ukljucuje se PRI OTVARANJU, a ne kasnije, jer se snimac umece izmedju
     * uredjaja i transporta - posle otvaranja tamo vise nema mesta. Trag raste
     * sa brojem transfera, pa nije podrazumevano ukljucen. */
    int32_t record_trace;
} g2710_open_options;

/* Napuni strukturu podrazumevanim vrednostima i tacnim `size`.
 *
 * Postoji da pozivalac ne bi morao da pamti sta je podrazumevano - i da
 * `size` ne bi mogao da promasi. */
G2710_API void G2710_CALL g2710_open_options_init(g2710_open_options* options);

/* Otvori uredjaj.
 *
 * NE zauzima ga i NE proverava identitet - to su g2710_begin i g2710_identify,
 * jer prvi korak moze uspeti a drugi ne.
 *
 * `out_device` se postavlja samo pri uspehu; pri gresci ostaje NULL. */
G2710_API g2710_status G2710_CALL g2710_open(const g2710_open_options* options,
                                             g2710_device** out_device);

/* Zatvori i oslobodi. NULL je dozvoljen i ne radi nista.
 *
 * Ako je prenos u toku, prvo se prekida - handle koji se zatvori usred
 * skeniranja ne sme ostaviti cip da skenira. */
G2710_API void G2710_CALL g2710_close(g2710_device* device);

/* ---------------------------------------------------------------------------
 * Greske
 * ------------------------------------------------------------------------- */

/* Detalj poslednje greske na OVOM handle-u, u UTF-8.
 *
 * Upisuje najvise `capacity` bajtova ukljucujuci zavrsnu nulu i vraca koliko
 * je bajtova POTREBNO (bez zavrsne nule). Ako je vraceno >= capacity, poruka
 * je skracena.
 *
 * `device` sme biti NULL - tada se vraca greska poslednjeg g2710_open-a na
 * ovoj niti, jer tada handle-a jos nema. */
G2710_API int32_t G2710_CALL g2710_last_error(const g2710_device* device, char* buffer,
                                              int32_t capacity);

/* Win32 kod uz poslednju gresku, ili 0. Za dijagnostiku - .NET ga upisuje u
 * izvestaj, ne prikazuje korisniku. */
G2710_API uint32_t G2710_CALL g2710_last_win32(const g2710_device* device);

/* ---------------------------------------------------------------------------
 * Identitet, stanje, bezbednost
 * ------------------------------------------------------------------------- */

/* Procitaj USB identitet i odbij sve sto nije G2710.
 *
 * \\.\Usbscan0 je DELJENO ime - iza njega moze stajati bilo koji uredjaj
 * vezan za usbscan.sys. Vendor komanda sa G2710 semantikom poslata tudjem
 * uredjaju je tacno ono sto ovaj projekat sebi zabranjuje. */
G2710_API g2710_status G2710_CALL g2710_identify(g2710_device* device);

/* Zauzmi ekskluzivnu sesiju. Od ovog trenutka uredjaj je nas. */
G2710_API g2710_status G2710_CALL g2710_begin(g2710_device* device);

/* Oslobodi sesiju. */
G2710_API g2710_status G2710_CALL g2710_end(g2710_device* device);

G2710_API g2710_device_state G2710_CALL g2710_state(const g2710_device* device);

/* Efektivni nivo: min(BuildSafetyCeiling, trazeni). Vraca 0 ako je handle
 * NULL. */
G2710_API int32_t G2710_CALL g2710_effective_safety_level(const g2710_device* device);

/* Plafon ugradjen u binarni fajl. Ne zavisi od handle-a i ne moze se podici. */
G2710_API int32_t G2710_CALL g2710_build_safety_ceiling(void);

/* Da li je motorni kod uopste preveden u ovaj binarni fajl. 0 znaci da paket
 * ne moze pomeriti glavu ni ako se to zatrazi. */
G2710_API int32_t G2710_CALL g2710_motor_path_compiled(void);

G2710_API uint32_t G2710_CALL g2710_abi_version(void);

/* Ko drzi uredjaj, kada je zauzet. Isti ugovor o baferu kao g2710_last_error.
 * Prazan string znaci da se ne moze utvrditi. */
G2710_API int32_t G2710_CALL g2710_current_owner(const g2710_device* device, char* buffer,
                                                 int32_t capacity);

/* ---------------------------------------------------------------------------
 * Operacije koje traju
 * ------------------------------------------------------------------------- */

/* Napredak, 0..100. Vraca 0 da zatrazi prekid, != 0 da se nastavi.
 *
 * STIZE SA RADNE NITI. Vidi odluku 3 na vrhu. */
typedef int32_t(G2710_CALL* g2710_progress_fn)(int32_t percent, void* user);

/* Red dnevnika. Nivo: 0 debug, 1 info, 2 upozorenje, 3 greska. */
typedef void(G2710_CALL* g2710_log_fn)(int32_t level, const char* message, void* user);

/* Dnevnik vazi za ceo handle. NULL iskljucuje. */
G2710_API void G2710_CALL g2710_set_log(g2710_device* device, g2710_log_fn log, void* user);

/* Upali lampu i sacekaj zagrevanje. Trazi nivo 2. */
G2710_API g2710_status G2710_CALL g2710_warmup(g2710_device* device, uint32_t warmup_ms,
                                               g2710_progress_fn progress, void* user);

/* Vrati glavu na pocetnu poziciju. Trazi nivo 3.
 *
 * Obavezno posle G2710_STATUS_TRANSPORT_LOST: pozicija glave je tada
 * nepoznata i nijedna druga operacija ne sme krenuti. */
G2710_API g2710_status G2710_CALL g2710_home(g2710_device* device,
                                             g2710_progress_fn progress, void* user);

/* Prekini sve u letu. JEDINA funkcija koja sme iz druge niti. */
G2710_API void G2710_CALL g2710_cancel(g2710_device* device);

/* ---------------------------------------------------------------------------
 * Skeniranje
 * ------------------------------------------------------------------------- */

typedef struct g2710_scan_request {
    uint32_t size;

    int32_t resolution;
    g2710_color_mode color_mode;
    int32_t bits_per_channel; /* 8 ili 16 */

    /* Oblast u pikselima na TRAZENOJ rezoluciji. Sve nule = cela povrsina. */
    int32_t left;
    int32_t top;
    int32_t width;
    int32_t height;

    /* 1.0 je bez korekcije. <= 0 se tumaci kao 1.0. */
    double gamma;

    /* Ako je != 0, dozvoljene su i rezolucije koje hardver nije potvrdio.
     * Dijagnostika i wizard ga postavljaju; WIA i TWAIN nikada. */
    int32_t allow_unqualified;
} g2710_scan_request;

G2710_API void G2710_CALL g2710_scan_request_init(g2710_scan_request* request);

/* Geometrija koja se zna tek kada prolaz pocne. */
typedef struct g2710_scan_info {
    uint32_t size;

    int32_t width_pixels;
    int32_t lines;
    int32_t bits_per_channel;
    int32_t channels;
    uint32_t bytes_per_line;

    /* Rezolucija na kojoj se STVARNO skenira; razlicita od trazene kada za
     * trazenu nema reda u tabeli hardvera. */
    int32_t native_resolution;

    /* 0 znaci da kalibracija nije pokrenuta i da slika nosi neujednacenost
     * senzora. Aplikacija to mora moci da kaze korisniku. */
    int32_t shading_applied;
} g2710_scan_info;

/* Izracunaj sta bi se desilo, BEZ diranja uredjaja.
 *
 * Ceo racun je statican, pa radi i kada skenera nema. Aplikacija time moze
 * pokazati velicinu slike pre nego sto se ista pomeri. `device` sme biti NULL. */
G2710_API g2710_status G2710_CALL g2710_plan_scan(const g2710_device* device,
                                                  const g2710_scan_request* request,
                                                  g2710_scan_info* out_info);

/* Pokreni prolaz. Trazi nivo 5.
 *
 * Odbija ako lampa ne gori - bez toga bi ceo prolaz prosao "uspesno" i dao
 * crnu sliku, otkaz koji se na tudjem racunaru ne razlikuje od pokvarenog
 * senzora. */
G2710_API g2710_status G2710_CALL g2710_scan_begin(g2710_device* device,
                                                   const g2710_scan_request* request,
                                                   g2710_scan_info* out_info);

/* Sledeci gotov red.
 *
 * `capacity` mora biti bar g2710_scan_info::bytes_per_line; manji bafer je
 * G2710_STATUS_INVALID_ARGUMENT, ne tiho skracivanje.
 *
 * `out_done` dobija 1 kada je slika gotova i tada red NIJE popunjen. */
G2710_API g2710_status G2710_CALL g2710_scan_read_line(g2710_device* device, uint8_t* buffer,
                                                       uint32_t capacity, int32_t* out_done);

/* Zatvori prolaz. Zove se i posle greske i posle otkazivanja.
 *
 * Prolaz koji se ne zatvori ostavlja cip da skenira i glavu da se krece.
 * g2710_close ovo radi sam, ali izricito zatvaranje je redovan put. */
G2710_API g2710_status G2710_CALL g2710_scan_end(g2710_device* device);

/* ---------------------------------------------------------------------------
 * Trag
 * ------------------------------------------------------------------------- */

/* Ispisi zabelezene transfere u fajl.
 *
 * Trag je ono sto se salje nazad kada nesto ne radi na tudjem racunaru, pa
 * mora biti dostupan iz aplikacije, ne samo iz CLI-ja.
 *
 * Snimanje se ukljucuje kroz g2710_open_options::record_trace. Ako nije bilo
 * ukljuceno, vraca G2710_STATUS_INVALID_STATE - a ne prazan fajl, koji izgleda
 * kao da se nista nije desilo. */
G2710_API g2710_status G2710_CALL g2710_write_trace(g2710_device* device, const char* path);

/* ---------------------------------------------------------------------------
 * Sta uredjaj ume
 * ------------------------------------------------------------------------- */

/* Tabela mogucnosti kao JSON. Isti ugovor o baferu kao g2710_last_error.
 *
 * `device` sme biti NULL: racun je statican i radi kada skenera nema. Bas zato
 * aplikacija moze ponuditi rezolucije pre nego sto se ista prikljuci.
 *
 * Isti tekst ispisuje i `g2710ctl capabilities --json`, jer ga proizvodi ista
 * funkcija u jezgru. Dva ispisa bi se razisla, pa bi aplikacija pokazivala
 * jedno a izvestaj drugo.
 *
 * `advertisable` u izlazu je jedino sto sme da se ponudi krajnjem korisniku;
 * ostalo postoji i moze se pozvati kroz dijagnostiku.
 *
 * Dodato u ABI 1.1. */
G2710_API int32_t G2710_CALL g2710_capabilities(char* buffer, int32_t capacity);

/* Koliko je transfera zabelezeno. 0 ako snimanje nije ukljuceno. */
G2710_API int32_t G2710_CALL g2710_trace_count(const g2710_device* device);

#ifdef __cplusplus
}  /* extern "C" */
#endif

#endif /* G2710_ABI_H */
