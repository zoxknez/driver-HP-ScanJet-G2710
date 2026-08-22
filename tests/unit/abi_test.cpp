// C ABI - granica preko koje .NET prica sa jezgrom.
//
// Testira se STATICKA varijanta, u istom procesu. Da se ucitavao DLL, on bi
// nosio SVOJU kopiju TransportProvider singletona i simulator iz test procesa
// ne bi imao nikakvog efekta - ista zamka koja je zabelezena za wiaharness.
//
// Ono sto se ovde drzi zakljucanim nije "radi li skeniranje" - to je vec
// pokriveno u integracionim testovima - nego da granica ne laze:
//
//   greska je povratna vrednost, ne izuzetak
//   bafer daje pozivalac, i premali bafer je greska a ne tiho skracivanje
//   plafon build-a se kroz ABI ne moze podici
//   handle koji se zatvori usred prolaza ne ostavlja cip da skenira

#include "g2710_abi.h"

#include <gtest/gtest.h>

#include <atomic>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

namespace {

// Uredjaj otvoren nad simulatorom, na trazenom nivou.
class AbiHandle {
public:
    explicit AbiHandle(int32_t level = 5, int32_t trace = 0) {
        g2710_open_options options;
        g2710_open_options_init(&options);
        options.transport = G2710_TRANSPORT_SIM;
        options.requested_safety_level = level;
        options.client_name = "abi-test";
        options.record_trace = trace;
        status_ = g2710_open(&options, &device_);
    }

    ~AbiHandle() { g2710_close(device_); }

    AbiHandle(const AbiHandle&) = delete;
    AbiHandle& operator=(const AbiHandle&) = delete;

    g2710_status status() const noexcept { return status_; }
    g2710_device* get() const noexcept { return device_; }
    operator g2710_device*() const noexcept { return device_; }

    // Uredjaj spreman za skeniranje.
    bool ready() {
        return status_ == G2710_STATUS_OK && g2710_identify(device_) == G2710_STATUS_OK &&
               g2710_begin(device_) == G2710_STATUS_OK;
    }

    std::string lastError() const {
        const int32_t needed = g2710_last_error(device_, nullptr, 0);
        if (needed <= 0) {
            return {};
        }
        std::string text(static_cast<std::size_t>(needed) + 1, '\0');
        g2710_last_error(device_, text.data(), needed + 1);
        text.resize(static_cast<std::size_t>(needed));
        return text;
    }

private:
    g2710_device* device_ = nullptr;
    g2710_status status_ = G2710_STATUS_INTERNAL;
};

g2710_scan_request smallScan() {
    g2710_scan_request request;
    g2710_scan_request_init(&request);
    request.resolution = 300;
    request.color_mode = G2710_COLOR;
    request.bits_per_channel = 8;
    request.width = 64;
    request.height = 8;
    request.allow_unqualified = 1;
    return request;
}

}  // namespace

// =============================================================================
// Ugovor sam po sebi
// =============================================================================

TEST(Abi, VersionIsTheOneTheHeaderDeclares) {
    const uint32_t expected = (static_cast<uint32_t>(G2710_ABI_VERSION_MAJOR) << 16) |
                              static_cast<uint32_t>(G2710_ABI_VERSION_MINOR);
    EXPECT_EQ(expected, g2710_abi_version());
}

TEST(Abi, EveryStatusHasAName) {
    // Bez ovoga bi novi kod dobio "unknown" u logu, i dijagnostika sa tudjeg
    // racunara bi bila neupotrebljiva bas kada zatreba.
    for (int code = G2710_STATUS_OK; code <= G2710_STATUS_INTERNAL; ++code) {
        const char* name = g2710_status_name(static_cast<g2710_status>(code));
        ASSERT_NE(nullptr, name) << "kod " << code;
        EXPECT_STRNE("unknown", name) << "kod " << code << " nema ime";
    }
    EXPECT_STREQ("unknown", g2710_status_name(static_cast<g2710_status>(9999)));
}

TEST(Abi, OptionsInitFillsSizeAndDefaults) {
    g2710_open_options options{};
    std::memset(&options, 0xCD, sizeof(options));
    g2710_open_options_init(&options);

    // `size` je jedini nacin da ABI zna koliko je polja pozivalac popunio.
    EXPECT_EQ(sizeof(g2710_open_options), options.size);
    EXPECT_EQ(G2710_TRANSPORT_USBSCAN, options.transport);

    // Podrazumevani nivo je 1, ne 5. Aplikacija koja zaboravi da ga postavi ne
    // sme dobiti pravo da pomera motor.
    EXPECT_EQ(1, options.requested_safety_level);
    EXPECT_EQ(0, options.record_trace);
}

TEST(Abi, TheBuildCeilingIsVisibleWithoutOpeningAnything) {
    const int32_t ceiling = g2710_build_safety_ceiling();
    EXPECT_GE(ceiling, 1);
    EXPECT_LE(ceiling, 5);

    // Ispod nivoa 3 motorni put ne sme ni biti preveden. Ovo je ista tvrdnja
    // koju tools/verify-safety-ceiling.ps1 meri nad simbolima - ovde je vidi i
    // aplikacija, pa moze reci coveku sta paket sme.
    if (ceiling < 3) {
        EXPECT_EQ(0, g2710_motor_path_compiled());
    }
}

// =============================================================================
// Otvaranje: sve sto pozivalac moze da promasi
// =============================================================================

TEST(Abi, OpeningWithNullOptionsIsRefusedAndExplained) {
    g2710_device* device = nullptr;
    EXPECT_EQ(G2710_STATUS_INVALID_ARGUMENT, g2710_open(nullptr, &device));
    EXPECT_EQ(nullptr, device);

    // Handle-a jos nema, pa poruka mora biti dostupna i bez njega.
    const int32_t needed = g2710_last_error(nullptr, nullptr, 0);
    EXPECT_GT(needed, 0);
}

TEST(Abi, OpeningWithoutTheOutParameterIsRefused) {
    g2710_open_options options;
    g2710_open_options_init(&options);
    EXPECT_EQ(G2710_STATUS_INVALID_ARGUMENT, g2710_open(&options, nullptr));
}

TEST(Abi, AnUninitialisedOptionsStructIsRefusedByItsSize) {
    g2710_open_options options{};
    options.size = 0;  // pozivalac nije zvao g2710_open_options_init
    options.requested_safety_level = 5;

    g2710_device* device = nullptr;
    EXPECT_EQ(G2710_STATUS_INVALID_ARGUMENT, g2710_open(&options, &device));
    EXPECT_EQ(nullptr, device);

    std::string message(256, '\0');
    g2710_last_error(nullptr, message.data(), 256);
    EXPECT_NE(std::string::npos, message.find("size"))
        << "poruka ne kaze sta je pogresno: " << message.c_str();
}

TEST(Abi, ASafetyLevelOutsideOneToFiveIsRefused) {
    for (int32_t level : {0, 6, -1, 99}) {
        g2710_open_options options;
        g2710_open_options_init(&options);
        options.transport = G2710_TRANSPORT_SIM;
        options.requested_safety_level = level;

        g2710_device* device = nullptr;
        EXPECT_EQ(G2710_STATUS_INVALID_ARGUMENT, g2710_open(&options, &device))
            << "nivo " << level;
        EXPECT_EQ(nullptr, device) << "nivo " << level;
    }
}

TEST(Abi, ClosingNullIsHarmless) {
    g2710_close(nullptr);  // ne sme se srusiti
}

// =============================================================================
// Bafer: ugovor koji se lako prekrsi
// =============================================================================

TEST(Abi, AskingForTheSizeWithoutABufferReturnsWhatIsNeeded) {
    AbiHandle handle{5};
    ASSERT_EQ(G2710_STATUS_OK, handle.status());

    // Izazovi gresku sa poznatom porukom.
    g2710_scan_request request = smallScan();
    request.bits_per_channel = 12;
    g2710_scan_info info{};
    ASSERT_EQ(G2710_STATUS_INVALID_ARGUMENT, g2710_plan_scan(handle, &request, &info));

    const int32_t needed = g2710_last_error(handle, nullptr, 0);
    ASSERT_GT(needed, 0);

    std::vector<char> buffer(static_cast<std::size_t>(needed) + 1, '\xCD');
    const int32_t again = g2710_last_error(handle, buffer.data(), needed + 1);
    EXPECT_EQ(needed, again);
    EXPECT_EQ('\0', buffer[static_cast<std::size_t>(needed)]);
    EXPECT_EQ(static_cast<std::size_t>(needed), std::strlen(buffer.data()));
}

TEST(Abi, ATooSmallBufferIsTruncatedButAlwaysTerminated) {
    AbiHandle handle{5};
    ASSERT_EQ(G2710_STATUS_OK, handle.status());

    g2710_scan_request request = smallScan();
    request.bits_per_channel = 12;
    g2710_scan_info info{};
    ASSERT_EQ(G2710_STATUS_INVALID_ARGUMENT, g2710_plan_scan(handle, &request, &info));

    char small[8];
    std::memset(small, '\xCD', sizeof(small));
    const int32_t needed = g2710_last_error(handle, small, sizeof(small));

    // Vraca se POTREBNO, ne upisano - pozivalac tako zna da je skraceno.
    EXPECT_GT(needed, static_cast<int32_t>(sizeof(small)));

    // Niska bez zavrsne nule je najbrzi put do rusenja .NET marshaller-a.
    EXPECT_EQ('\0', small[sizeof(small) - 1]);
    EXPECT_EQ(sizeof(small) - 1, std::strlen(small));
}

// =============================================================================
// Bezbednost
// =============================================================================

TEST(Abi, TheEffectiveLevelIsTheMinimumOfCeilingAndRequest) {
    AbiHandle handle{5};
    ASSERT_EQ(G2710_STATUS_OK, handle.status());

    const int32_t ceiling = g2710_build_safety_ceiling();
    EXPECT_EQ(std::min<int32_t>(ceiling, 5), g2710_effective_safety_level(handle));
}

TEST(Abi, RequestingLessThanTheCeilingStillGivesLess) {
    // Plafon spusta, ali ne DIZE. Nivo 1 mora ostati nivo 1 i u build-u koji
    // sme sve.
    AbiHandle handle{1};
    ASSERT_EQ(G2710_STATUS_OK, handle.status());
    EXPECT_EQ(1, g2710_effective_safety_level(handle));
}

TEST(Abi, WarmupIsRefusedBelowLevelTwo) {
    AbiHandle handle{1};
    ASSERT_TRUE(handle.ready());

    EXPECT_EQ(G2710_STATUS_SAFETY_VIOLATION, g2710_warmup(handle, 10, nullptr, nullptr));
}

TEST(Abi, HomeReportsTheMissingPortNotAFakeSuccess) {
    AbiHandle handle{5};
    ASSERT_TRUE(handle.ready());

    if (g2710_effective_safety_level(handle) < 3) {
        GTEST_SKIP() << "plafon build-a je ispod 3";
    }

    // Kretanje glave trazi port Head_Relocate, koga jos nema. Lagati da je
    // proslo znacilo bi da aplikacija misli da zna gde je glava.
    EXPECT_EQ(G2710_STATUS_NOT_IMPLEMENTED, g2710_home(handle, nullptr, nullptr));

    std::string message(256, '\0');
    g2710_last_error(handle, message.data(), 256);
    EXPECT_NE(std::string::npos, message.find("Head_Relocate"))
        << "razlog nije naveden: " << message.c_str();
}

TEST(Abi, HomeBelowLevelThreeSaysSafetyNotUnimplemented) {
    AbiHandle handle{1};
    ASSERT_TRUE(handle.ready());

    // Redosled provera je vazan: paketu sa plafonom 1 tacan odgovor je "ovaj
    // paket to ne sme", a ne "nije implementirano".
    EXPECT_EQ(G2710_STATUS_SAFETY_VIOLATION, g2710_home(handle, nullptr, nullptr));
}

// =============================================================================
// Planiranje bez uredjaja
// =============================================================================

TEST(Abi, PlanningWorksWithoutAnyDevice) {
    // Ceo racun je statican. Aplikacija time pokazuje velicinu slike pre nego
    // sto se ista pomeri - i pre nego sto skener uopste postoji.
    g2710_scan_request request = smallScan();
    g2710_scan_info info{};

    ASSERT_EQ(G2710_STATUS_OK, g2710_plan_scan(nullptr, &request, &info));
    EXPECT_EQ(sizeof(g2710_scan_info), info.size);
    EXPECT_EQ(64, info.width_pixels);
    EXPECT_EQ(3, info.channels);
    EXPECT_EQ(8, info.bits_per_channel);
    EXPECT_EQ(64u * 3u, info.bytes_per_line);
    EXPECT_EQ(300, info.native_resolution);
}

TEST(Abi, PlanningReportsWhereItWillReallyScan) {
    // 200 dpi nema svoj red u tabeli hardvera - skenira se na 300 pa smanjuje.
    // Aplikacija to mora moci da kaze korisniku.
    g2710_scan_request request = smallScan();
    request.resolution = 200;
    g2710_scan_info info{};

    ASSERT_EQ(G2710_STATUS_OK, g2710_plan_scan(nullptr, &request, &info));
    EXPECT_EQ(300, info.native_resolution);
}

TEST(Abi, LineartIsOneBitPerChannel) {
    g2710_scan_request request = smallScan();
    request.color_mode = G2710_LINEART;
    g2710_scan_info info{};

    ASSERT_EQ(G2710_STATUS_OK, g2710_plan_scan(nullptr, &request, &info));
    EXPECT_EQ(1, info.channels);
    EXPECT_EQ(1, info.bits_per_channel);
}

TEST(Abi, AnUninitialisedScanRequestIsRefusedByItsSize) {
    g2710_scan_request request{};
    request.size = 0;
    g2710_scan_info info{};
    EXPECT_EQ(G2710_STATUS_INVALID_ARGUMENT, g2710_plan_scan(nullptr, &request, &info));
}

TEST(Abi, ADepthOtherThanEightOrSixteenIsRefused) {
    g2710_scan_request request = smallScan();
    request.bits_per_channel = 12;
    g2710_scan_info info{};
    EXPECT_EQ(G2710_STATUS_INVALID_ARGUMENT, g2710_plan_scan(nullptr, &request, &info));
}

TEST(Abi, ANegativeRegionIsRefused) {
    g2710_scan_request request = smallScan();
    request.left = -1;
    g2710_scan_info info{};
    EXPECT_EQ(G2710_STATUS_INVALID_ARGUMENT, g2710_plan_scan(nullptr, &request, &info));
}

// =============================================================================
// Pun tok
// =============================================================================

TEST(Abi, TheWholeFlowDeliversAnImage) {
    AbiHandle handle{5};
    ASSERT_TRUE(handle.ready());
    ASSERT_EQ(G2710_STATE_IDLE, g2710_state(handle));

    ASSERT_EQ(G2710_STATUS_OK, g2710_warmup(handle, 20, nullptr, nullptr));

    const g2710_scan_request request = smallScan();
    g2710_scan_info info{};
    ASSERT_EQ(G2710_STATUS_OK, g2710_scan_begin(handle, &request, &info))
        << handle.lastError();

    ASSERT_GT(info.bytes_per_line, 0u);
    ASSERT_GT(info.lines, 0);

    std::vector<uint8_t> line(info.bytes_per_line);
    int delivered = 0;
    for (;;) {
        int32_t done = 0;
        ASSERT_EQ(G2710_STATUS_OK,
                  g2710_scan_read_line(handle, line.data(),
                                       static_cast<uint32_t>(line.size()), &done))
            << handle.lastError();
        if (done != 0) {
            break;
        }
        ++delivered;
        ASSERT_LE(delivered, info.lines * 2) << "prolaz ne staje";
    }

    EXPECT_EQ(info.lines, delivered);
    EXPECT_EQ(G2710_STATUS_OK, g2710_scan_end(handle));
    EXPECT_EQ(G2710_STATUS_OK, g2710_end(handle));
}

TEST(Abi, ATooSmallLineBufferIsRefusedInsteadOfTruncated) {
    AbiHandle handle{5};
    ASSERT_TRUE(handle.ready());
    ASSERT_EQ(G2710_STATUS_OK, g2710_warmup(handle, 20, nullptr, nullptr));

    const g2710_scan_request request = smallScan();
    g2710_scan_info info{};
    ASSERT_EQ(G2710_STATUS_OK, g2710_scan_begin(handle, &request, &info));

    // Skracena slika izgleda kao pokvaren skener; greska izgleda kao greska.
    std::vector<uint8_t> tooSmall(info.bytes_per_line - 1);
    int32_t done = 0;
    EXPECT_EQ(G2710_STATUS_INVALID_ARGUMENT,
              g2710_scan_read_line(handle, tooSmall.data(),
                                   static_cast<uint32_t>(tooSmall.size()), &done));

    EXPECT_EQ(G2710_STATUS_OK, g2710_scan_end(handle));
}

TEST(Abi, ReadingWithoutBeginningIsRefused) {
    AbiHandle handle{5};
    ASSERT_TRUE(handle.ready());

    std::vector<uint8_t> line(1024);
    int32_t done = 0;
    EXPECT_EQ(G2710_STATUS_INVALID_STATE,
              g2710_scan_read_line(handle, line.data(),
                                   static_cast<uint32_t>(line.size()), &done));
}

TEST(Abi, BeginningTwiceIsRefusedInsteadOfSilentlyReplacing) {
    AbiHandle handle{5};
    ASSERT_TRUE(handle.ready());
    ASSERT_EQ(G2710_STATUS_OK, g2710_warmup(handle, 20, nullptr, nullptr));

    const g2710_scan_request request = smallScan();
    g2710_scan_info info{};
    ASSERT_EQ(G2710_STATUS_OK, g2710_scan_begin(handle, &request, &info));

    // Tiha zamena bi ostavila cip da skenira dok se pocinje novi prolaz.
    EXPECT_EQ(G2710_STATUS_INVALID_STATE, g2710_scan_begin(handle, &request, &info));
    EXPECT_EQ(G2710_STATUS_OK, g2710_scan_end(handle));
}

TEST(Abi, EndingAScanThatNeverStartedIsNotAnError) {
    AbiHandle handle{5};
    ASSERT_TRUE(handle.ready());

    // Pozivalac ovo radi u `finally` bloku i ne zna uvek da li je pocelo.
    EXPECT_EQ(G2710_STATUS_OK, g2710_scan_end(handle));
}

TEST(Abi, TwoScansInARowBothSucceed) {
    AbiHandle handle{5};
    ASSERT_TRUE(handle.ready());
    ASSERT_EQ(G2710_STATUS_OK, g2710_warmup(handle, 20, nullptr, nullptr));

    const g2710_scan_request request = smallScan();
    for (int pass = 0; pass < 2; ++pass) {
        g2710_scan_info info{};
        ASSERT_EQ(G2710_STATUS_OK, g2710_scan_begin(handle, &request, &info))
            << "prolaz " << pass << ": " << handle.lastError();

        std::vector<uint8_t> line(info.bytes_per_line);
        int32_t done = 0;
        int delivered = 0;
        while (g2710_scan_read_line(handle, line.data(),
                                    static_cast<uint32_t>(line.size()),
                                    &done) == G2710_STATUS_OK &&
               done == 0) {
            ++delivered;
        }
        EXPECT_EQ(info.lines, delivered) << "prolaz " << pass;
        ASSERT_EQ(G2710_STATUS_OK, g2710_scan_end(handle)) << "prolaz " << pass;
    }
}

TEST(Abi, ClosingMidScanDoesNotLeaveTheChipScanning) {
    const g2710_scan_request request = smallScan();

    {
        AbiHandle handle{5};
        ASSERT_TRUE(handle.ready());
        ASSERT_EQ(G2710_STATUS_OK, g2710_warmup(handle, 20, nullptr, nullptr));

        g2710_scan_info info{};
        ASSERT_EQ(G2710_STATUS_OK, g2710_scan_begin(handle, &request, &info));

        std::vector<uint8_t> line(info.bytes_per_line);
        int32_t done = 0;
        ASSERT_EQ(G2710_STATUS_OK,
                  g2710_scan_read_line(handle, line.data(),
                                       static_cast<uint32_t>(line.size()), &done));
        // Namerno bez scan_end - handle se zatvara usred prolaza.
    }

    // Da prvi handle nije zatvorio prolaz, drugi bi zatekao zauzet uredjaj.
    AbiHandle second{5};
    ASSERT_TRUE(second.ready());
    ASSERT_EQ(G2710_STATUS_OK, g2710_warmup(second, 20, nullptr, nullptr));

    g2710_scan_info info{};
    EXPECT_EQ(G2710_STATUS_OK, g2710_scan_begin(second, &request, &info))
        << second.lastError();
    EXPECT_EQ(G2710_STATUS_OK, g2710_scan_end(second));
}

// =============================================================================
// Otkazivanje
// =============================================================================

TEST(Abi, CancelStopsAScanInProgress) {
    AbiHandle handle{5};
    ASSERT_TRUE(handle.ready());
    ASSERT_EQ(G2710_STATUS_OK, g2710_warmup(handle, 20, nullptr, nullptr));

    g2710_scan_request request = smallScan();
    request.height = 200;
    g2710_scan_info info{};
    ASSERT_EQ(G2710_STATUS_OK, g2710_scan_begin(handle, &request, &info));

    std::vector<uint8_t> line(info.bytes_per_line);
    int32_t done = 0;
    ASSERT_EQ(G2710_STATUS_OK,
              g2710_scan_read_line(handle, line.data(),
                                   static_cast<uint32_t>(line.size()), &done));

    g2710_cancel(handle);

    EXPECT_EQ(G2710_STATUS_CANCELLED,
              g2710_scan_read_line(handle, line.data(),
                                   static_cast<uint32_t>(line.size()), &done))
        << "lines=" << info.lines << " done=" << done
        << " state=" << static_cast<int>(g2710_state(handle))
        << " err=" << handle.lastError();
    EXPECT_EQ(G2710_STATUS_OK, g2710_scan_end(handle));
}

TEST(Abi, CancelFromAnotherThreadIsAllowed) {
    AbiHandle handle{5};
    ASSERT_TRUE(handle.ready());
    ASSERT_EQ(G2710_STATUS_OK, g2710_warmup(handle, 20, nullptr, nullptr));

    g2710_scan_request request = smallScan();
    request.height = 400;
    g2710_scan_info info{};
    ASSERT_EQ(G2710_STATUS_OK, g2710_scan_begin(handle, &request, &info));

    // Rukovanje, ne trka.
    //
    // Prva verzija je spavala 5 ms i nadala se da ce otkazivanje stici pre
    // kraja prolaza. Simulator isporuci 400 redova za manje od toga, pa je test
    // padao - i to bi izgledalo kao da otkazivanje ne radi, umesto kao da test
    // ne meri ono sto tvrdi.
    std::atomic<bool> lineRead{false};
    std::atomic<bool> cancelled{false};

    std::thread stopper([&] {
        while (!lineRead.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }
        // g2710_cancel je JEDINA funkcija koja sme iz druge niti - i to je i
        // njena svrha: dugme "Prekini" ne zivi na niti koja skenira.
        g2710_cancel(handle.get());
        cancelled.store(true, std::memory_order_release);
    });

    std::vector<uint8_t> line(info.bytes_per_line);
    int32_t done = 0;
    ASSERT_EQ(G2710_STATUS_OK,
              g2710_scan_read_line(handle, line.data(),
                                   static_cast<uint32_t>(line.size()), &done));
    ASSERT_EQ(0, done);

    lineRead.store(true, std::memory_order_release);
    while (!cancelled.load(std::memory_order_acquire)) {
        std::this_thread::yield();
    }
    stopper.join();

    EXPECT_EQ(G2710_STATUS_CANCELLED,
              g2710_scan_read_line(handle, line.data(),
                                   static_cast<uint32_t>(line.size()), &done));

    // Otkazan prolaz se MORA moci zatvoriti. Zaustavljanje cipa je i samo
    // transfer, pa ga lepljivi cancel odbija ako se prethodno ne ponisti.
    EXPECT_EQ(G2710_STATUS_OK, g2710_scan_end(handle)) << handle.lastError();
}

TEST(Abi, AProgressCallbackThatReturnsZeroStopsTheWarmup) {
    AbiHandle handle{5};
    ASSERT_TRUE(handle.ready());

    struct Counter {
        int calls = 0;
    } counter;

    auto callback = [](int32_t percent, void* user) -> int32_t {
        auto* c = static_cast<Counter*>(user);
        ++c->calls;
        (void)percent;
        return c->calls < 3 ? 1 : 0;  // treci poziv trazi prekid
    };

    EXPECT_EQ(G2710_STATUS_CANCELLED, g2710_warmup(handle, 200, callback, &counter));
    EXPECT_EQ(3, counter.calls);
}

// =============================================================================
// Dnevnik
// =============================================================================

TEST(Abi, TheLogReceivesTheReasonAnOperationFailed) {
    AbiHandle handle{5};
    ASSERT_EQ(G2710_STATUS_OK, handle.status());

    std::vector<std::string> lines;
    g2710_set_log(
        handle,
        [](int32_t level, const char* message, void* user) {
            (void)level;
            static_cast<std::vector<std::string>*>(user)->emplace_back(message);
        },
        &lines);

    g2710_scan_request request = smallScan();
    request.bits_per_channel = 12;
    g2710_scan_info info{};
    ASSERT_EQ(G2710_STATUS_INVALID_ARGUMENT, g2710_plan_scan(handle, &request, &info));

    ASSERT_FALSE(lines.empty()) << "greska nije stigla u dnevnik";
    EXPECT_NE(std::string::npos, lines.back().find("bits_per_channel"));

    // Iskljucivanje mora zaista iskljuciti.
    g2710_set_log(handle, nullptr, nullptr);
    const std::size_t before = lines.size();
    ASSERT_EQ(G2710_STATUS_INVALID_ARGUMENT, g2710_plan_scan(handle, &request, &info));
    EXPECT_EQ(before, lines.size());
}

// =============================================================================
// Trag
// =============================================================================

TEST(Abi, WithoutRecordingTheTraceIsRefusedNotEmpty) {
    AbiHandle handle{5};
    ASSERT_TRUE(handle.ready());

    // Prazan fajl bi izgledao kao da se nista nije desilo.
    EXPECT_EQ(0, g2710_trace_count(handle));
    EXPECT_EQ(G2710_STATUS_INVALID_STATE, g2710_write_trace(handle, "nepostojeci.trace"));
}

TEST(Abi, RecordingCapturesTransfersAndWritesThemOut) {
    AbiHandle handle{5, /*trace=*/1};
    ASSERT_TRUE(handle.ready());

    EXPECT_GT(g2710_trace_count(handle), 0) << "identify nije zabelezen";

    const std::string path =
        (std::filesystem::temp_directory_path() / "g2710-abi-test.trace").string();
    ASSERT_EQ(G2710_STATUS_OK, g2710_write_trace(handle, path.c_str()))
        << handle.lastError();

    std::ifstream file(path, std::ios::binary);
    ASSERT_TRUE(file.good());
    const std::string text((std::istreambuf_iterator<char>(file)),
                           std::istreambuf_iterator<char>());
    file.close();
    std::filesystem::remove(path);

    EXPECT_FALSE(text.empty());
}
