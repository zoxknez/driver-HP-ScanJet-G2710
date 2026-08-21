// Ceo cevovod skeniranja, od registara do gotovih redova.
//
// Sesija je jedino mesto gde se plan, cip i obrada slike sretnu, pa je ovo
// jedini test koji moze da uhvati gresku u SPOJU - tacan modul koji sve radi
// kako treba, a zajedno daju pogresnu sliku.
//
// Dve grupe nose najvise:
//
//   - odbijanje kada lampa ne gori. Bez toga bi prolaz "uspeo" i dao crnu
//     sliku, sto se na tudjem racunaru ne razlikuje od pokvarenog senzora.
//   - softversko poravnanje na 1200 dpi. Tu se D3 ne samo vidi nego i
//     ZAOBILAZI: hardverska polja se gase, a LineOffsetCorrector vraca kanale
//     na mesto.

#include "SimTransport.h"
#include "rts8822/Lamp.h"
#include "rts8822/RegisterFile.h"
#include "rts8822/Registers.h"
#include "scan/ScanPlanner.h"
#include "scan/ScanSession.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <set>
#include <vector>

using namespace g2710;
using namespace g2710::scan;

namespace {

constexpr int kMotorResolution = 1200;
constexpr int kStripBoundarySteps = static_cast<int>((5.0 / 25.4) * kMotorResolution);

class ScanSessionTest : public ::testing::Test {
protected:
    sim::SimTransport device;
    rts8822::RegisterFile registers{device};
    SafetyGate gate{SafetyLevel::FullScan};

    void SetUp() override { device.ccd().makeIdeal(); }

    void turnLampOn() {
        rts8822::Lamp lamp{registers, gate};
        ASSERT_TRUE(lamp.setLamp(rts8822::LampKind::Flatbed, true));
        ASSERT_TRUE(lamp.setupPwm(rts8822::LampKind::Flatbed));
        device.advanceTime(60000);
    }

    ScanPlan makePlan(int dpi, image::ColorMode mode, int depth, int width, int height) {
        ScanRequest request;
        request.resolution = dpi;
        request.colorMode = mode;
        request.depth = depth;
        request.region = {0, 0, width, height};
        request.allowUnqualified = true;

        auto planned = planScan(request);
        EXPECT_TRUE(planned) << dpi << " " << toString(mode);
        return planned ? planned.value() : ScanPlan{};
    }

    // Isprazni ceo prolaz i vrati redove.
    std::vector<std::vector<std::uint8_t>> run(ScanSession& session) {
        std::vector<std::vector<std::uint8_t>> lines;
        std::vector<std::uint8_t> line(session.outputBytesPerLine(), 0);
        const CancellationToken token = CancellationToken::never();

        for (;;) {
            auto more = session.nextLine(line, token);
            EXPECT_TRUE(more) << (more ? "" : more.error().context);
            if (!more || !more.value()) {
                break;
            }
            lines.push_back(line);
        }
        return lines;
    }

    // Prvi red u kome dati kanal padne ispod pola.
    int transitionLine(const std::vector<std::vector<std::uint8_t>>& lines, int channels,
                       int channel) const {
        for (std::size_t i = 0; i < lines.size(); ++i) {
            const std::size_t pixel = (lines[i].size() / channels) / 2;
            const std::size_t index = pixel * channels + static_cast<std::size_t>(channel);
            if (index < lines[i].size() && lines[i][index] < 0x80) {
                return static_cast<int>(i);
            }
        }
        return -1;
    }
};

}  // namespace

// --- lampa --------------------------------------------------------------------

TEST_F(ScanSessionTest, RefusesToStartWithTheLampOff) {
    const ScanPlan plan = makePlan(300, image::ColorMode::Color, 8, 64, 10);
    ScanSession session{registers, gate, plan};

    const Status begun = session.begin();
    ASSERT_FALSE(begun) << "prolaz bi dao crnu sliku, a izgledao bi uspesno";
    EXPECT_EQ(begun.error().code, ErrorCode::InvalidState);
    EXPECT_FALSE(session.started());
}

TEST_F(ScanSessionTest, StartsOnceTheLampIsLit) {
    turnLampOn();
    const ScanPlan plan = makePlan(300, image::ColorMode::Color, 8, 64, 10);
    ScanSession session{registers, gate, plan};
    EXPECT_TRUE(session.begin());
    EXPECT_TRUE(session.started());
}

TEST_F(ScanSessionTest, ReadingBeforeBeginIsRefused) {
    turnLampOn();
    const ScanPlan plan = makePlan(300, image::ColorMode::Color, 8, 64, 10);
    ScanSession session{registers, gate, plan};

    std::vector<std::uint8_t> line(session.outputBytesPerLine(), 0);
    const auto more = session.nextLine(line, CancellationToken::never());
    ASSERT_FALSE(more);
    EXPECT_EQ(more.error().code, ErrorCode::InvalidState);
}

// --- geometrija izlaza ---------------------------------------------------------

TEST_F(ScanSessionTest, ProducesExactlyTheRequestedNumberOfLines) {
    turnLampOn();
    const ScanPlan plan = makePlan(300, image::ColorMode::Color, 8, 64, 25);
    ScanSession session{registers, gate, plan};
    ASSERT_TRUE(session.begin());

    const auto lines = run(session);
    EXPECT_EQ(lines.size(), 25u);
    EXPECT_EQ(session.statistics().outputLinesProduced, 25);
}

TEST_F(ScanSessionTest, LineLengthFollowsModeAndDepth) {
    turnLampOn();

    const struct {
        image::ColorMode mode;
        int depth;
        std::size_t expected;
    } cases[] = {
        {image::ColorMode::Color, 8, 64u * 3},
        {image::ColorMode::Color, 16, 64u * 3 * 2},
        {image::ColorMode::Gray, 8, 64u},
        {image::ColorMode::Gray, 16, 64u * 2},
        {image::ColorMode::Lineart, 8, 8u},
    };

    for (const auto& testCase : cases) {
        const ScanPlan plan = makePlan(300, testCase.mode, testCase.depth, 64, 6);
        ScanSession session{registers, gate, plan};
        EXPECT_EQ(session.outputBytesPerLine(), testCase.expected)
            << toString(testCase.mode) << " " << testCase.depth;

        ASSERT_TRUE(session.begin());
        const auto lines = run(session);
        ASSERT_FALSE(lines.empty());
        EXPECT_EQ(lines[0].size(), testCase.expected);
        EXPECT_EQ(lines.size(), 6u);
    }
}

TEST_F(ScanSessionTest, WrongBufferSizeIsRefused) {
    turnLampOn();
    const ScanPlan plan = makePlan(300, image::ColorMode::Color, 8, 64, 6);
    ScanSession session{registers, gate, plan};
    ASSERT_TRUE(session.begin());

    std::vector<std::uint8_t> tooSmall(session.outputBytesPerLine() - 1, 0);
    const auto refused = session.nextLine(tooSmall, CancellationToken::never());
    ASSERT_FALSE(refused);
    EXPECT_EQ(refused.error().code, ErrorCode::InvalidArgument);
}

// --- sadrzaj -------------------------------------------------------------------

TEST_F(ScanSessionTest, WhiteStripComesOutBrightAndBlackStripDark) {
    turnLampOn();

    device.motor().teleportTo(24);
    ScanPlan plan = makePlan(300, image::ColorMode::Gray, 8, 64, 6);
    {
        ScanSession session{registers, gate, plan};
        ASSERT_TRUE(session.begin());
        const auto lines = run(session);
        ASSERT_FALSE(lines.empty());
        EXPECT_GT(lines[0][32], 0xA0) << "bela traka";
    }

    device.motor().teleportTo(kStripBoundarySteps + 24);
    {
        ScanSession session{registers, gate, plan};
        ASSERT_TRUE(session.begin());
        const auto lines = run(session);
        ASSERT_FALSE(lines.empty());
        EXPECT_LT(lines[0][32], 0x30) << "crna traka";
    }
}

TEST_F(ScanSessionTest, SixteenBitOutputIsLittleEndianAndCarriesMorePrecision) {
    turnLampOn();
    device.motor().teleportTo(24);

    const ScanPlan plan = makePlan(300, image::ColorMode::Gray, 16, 64, 4);
    ScanSession session{registers, gate, plan};
    ASSERT_TRUE(session.begin());

    const auto lines = run(session);
    ASSERT_FALSE(lines.empty());
    ASSERT_EQ(lines[0].size(), 128u);

    const std::uint16_t value =
        static_cast<std::uint16_t>(lines[0][64] | (lines[0][65] << 8));
    EXPECT_GT(value, 0xA000) << "bela traka u punoj 16-bitnoj skali";

    // Cevovod je little-endian: visi bajt je na neparnom mestu. Za svetlu
    // traku on mora biti veliki, a osmobitni prolaz iste trake mora dati
    // priblizno bas njega.
    EXPECT_GT(lines[0][65], 0xA0) << "visi bajt na neparnom mestu";

    const ScanPlan eightBit = makePlan(300, image::ColorMode::Gray, 8, 64, 4);
    ScanSession narrow{registers, gate, eightBit};
    ASSERT_TRUE(narrow.begin());
    const auto narrowLines = run(narrow);
    ASSERT_FALSE(narrowLines.empty());
    EXPECT_NEAR(narrowLines[0][32], lines[0][65], 2)
        << "osam bita mora biti visi bajt sesnaestobitnog";
}

TEST_F(ScanSessionTest, GammaChangesTheResult) {
    turnLampOn();
    device.motor().teleportTo(24);

    const ScanPlan plan = makePlan(300, image::ColorMode::Gray, 8, 64, 4);

    std::vector<std::uint8_t> plain;
    {
        ScanSession session{registers, gate, plan};
        ASSERT_TRUE(session.begin());
        const auto lines = run(session);
        ASSERT_FALSE(lines.empty());
        plain = lines[0];
    }

    ScanOptions options;
    options.gamma = image::makeGammaTable(2.2);
    {
        ScanSession session{registers, gate, plan, options};
        EXPECT_TRUE(session.gammaApplied());
        ASSERT_TRUE(session.begin());
        const auto lines = run(session);
        ASSERT_FALSE(lines.empty());
        EXPECT_NE(lines[0], plain) << "gamma nije nista promenila";
    }
}

TEST_F(ScanSessionTest, ShadingIsReportedAsNotAppliedWhenThereIsNone) {
    turnLampOn();
    const ScanPlan plan = makePlan(300, image::ColorMode::Color, 8, 64, 4);
    ScanSession session{registers, gate, plan};

    EXPECT_FALSE(session.shadingApplied())
        << "izostanak kalibracije ne sme proci neprimeceno";
    EXPECT_FALSE(session.gammaApplied());
}

// --- smanjivanje ----------------------------------------------------------------

TEST_F(ScanSessionTest, NonNativeResolutionScansHigherAndShrinks) {
    turnLampOn();
    const ScanPlan plan = makePlan(100, image::ColorMode::Color, 8, 300, 60);

    ASSERT_EQ(plan.nativeResolution, 150);
    ASSERT_EQ(plan.resize, ResizeType::Decrease);

    ScanSession session{registers, gate, plan};
    ASSERT_TRUE(session.begin());

    const auto lines = run(session);
    EXPECT_EQ(lines.size(), 60u);
    EXPECT_EQ(lines[0].size(), 300u * 3);

    // Odnos je 3:2, pa se na svaka tri hardverska reda izdaju dva izlazna.
    const auto& stats = session.statistics();
    EXPECT_GT(stats.hardwareLinesRead, 60);
    EXPECT_GT(stats.resampleLinesConsumed, 0);
    EXPECT_EQ(stats.hardwareLinesRead, stats.outputLinesProduced + stats.resampleLinesConsumed);
}

// Brojevi redova ne dokazuju da smanjivanje racuna. Ovaj test gleda SADRZAJ:
// preko RGB stubica izlazni red mora ostati sarolik, a ne postati ravan.
TEST_F(ScanSessionTest, ShrinkingKeepsHorizontalDetail) {
    turnLampOn();

    // 10-40 mm su RGB stubici; na 150 dpi to je od 59. reda nadalje.
    device.motor().teleportTo(static_cast<int>((12.0 / 25.4) * kMotorResolution));

    const ScanPlan plan = makePlan(100, image::ColorMode::Color, 8, 300, 20);
    ASSERT_EQ(plan.resize, ResizeType::Decrease);

    ScanSession session{registers, gate, plan};
    ASSERT_TRUE(session.begin());
    const auto lines = run(session);
    ASSERT_FALSE(lines.empty());

    std::set<std::uint8_t> reds;
    for (std::size_t pixel = 0; pixel < 300; ++pixel) {
        reds.insert(lines[0][pixel * 3]);
    }
    EXPECT_GT(reds.size(), 1u) << "smanjivanje je spljostilo red u jednu vrednost";
}

// Isto uspravno, ali preko granice bele i crne trake - RGB stubici su uspravni,
// pa se niz njih redovi i ne razlikuju.
TEST_F(ScanSessionTest, ShrinkingKeepsVerticalDetail) {
    turnLampOn();
    device.motor().teleportTo(kStripBoundarySteps - 60);

    const ScanPlan plan = makePlan(100, image::ColorMode::Gray, 8, 64, 20);
    ASSERT_EQ(plan.resize, ResizeType::Decrease);

    ScanSession session{registers, gate, plan};
    ASSERT_TRUE(session.begin());
    const auto lines = run(session);
    ASSERT_EQ(lines.size(), 20u);

    EXPECT_GT(lines.front()[32], 0xA0) << "pocetak je na beloj traci";
    EXPECT_LT(lines.back()[32], 0x30) << "kraj je na crnoj traci";
}

TEST_F(ScanSessionTest, LineartSurvivesTheResizePath) {
    turnLampOn();
    device.motor().teleportTo(24);

    const ScanPlan plan = makePlan(100, image::ColorMode::Lineart, 8, 300, 40);
    ASSERT_EQ(plan.resize, ResizeType::Decrease);

    ScanSession session{registers, gate, plan};
    ASSERT_TRUE(session.begin());

    const auto lines = run(session);
    EXPECT_EQ(lines.size(), 40u);
    EXPECT_EQ(lines[0].size(), (300u + 7) / 8);
}

// --- poravnanje kanala i D3 --------------------------------------------------------

// Do 600 dpi poravnanje radi hardver.
TEST_F(ScanSessionTest, HardwareAlignmentIsUsedAtThreeHundredDpi) {
    turnLampOn();
    device.motor().teleportTo(kStripBoundarySteps - 40);

    const ScanPlan plan = makePlan(300, image::ColorMode::Color, 8, 64, 30);
    ASSERT_TRUE(plan.useHardwareAlignment);

    ScanSession session{registers, gate, plan};
    ASSERT_TRUE(session.begin());
    const auto lines = run(session);

    const int red = transitionLine(lines, 3, 0);
    ASSERT_GT(red, 0);
    EXPECT_EQ(transitionLine(lines, 3, 1), red) << "zeleni";
    EXPECT_EQ(transitionLine(lines, 3, 2), red) << "plavi";
    EXPECT_EQ(session.statistics().alignmentLinesConsumed, 0)
        << "softversko poravnanje se nije ni ukljucilo";
}

// Na 1200 dpi hardver ne moze - polje se odseca. Sesija zato GASI hardversko
// poravnanje i posao preuzima LineOffsetCorrector. Ovo je zaobilazak D3, i
// mora se videti u rezultatu: kanali izlaze poravnati uprkos defektu.
TEST_F(ScanSessionTest, SoftwareAlignmentRescuesTwelveHundredDpi) {
    turnLampOn();
    device.motor().teleportTo(kStripBoundarySteps - 300);

    const ScanPlan plan = makePlan(1200, image::ColorMode::Color, 8, 64, 400);
    ASSERT_FALSE(plan.useHardwareAlignment) << "na 1200 dpi polje ne staje u sest bita";
    ASSERT_EQ(plan.softwareLineDistance, 32);

    // Napuni polja PRE prolaza. Bez ovoga bi nula posle begin() mogla znaciti
    // i da ih niko nije ni dirao.
    rts8822::ScanRegisters check{registers, gate};
    ASSERT_TRUE(check.setLineOffsets({7, 21, 28, 42, 49}));
    ASSERT_EQ(check.lineOffsets().value().largest(), 49);

    ScanSession session{registers, gate, plan};
    ASSERT_TRUE(session.begin());

    // Hardverska polja moraju biti UGASENA - inace bi cip pomerio kanale
    // napola i softver bi ih pomerio jos jednom.
    const auto offsets = check.lineOffsets();
    ASSERT_TRUE(offsets);
    EXPECT_EQ(offsets.value().largest(), 0) << "polja nisu obrisana";

    const auto lines = run(session);
    ASSERT_FALSE(lines.empty());

    EXPECT_GT(session.statistics().alignmentLinesConsumed, 0)
        << "korektor mora prvo da napuni cevovod";

    const int red = transitionLine(lines, 3, 0);
    const int green = transitionLine(lines, 3, 1);
    const int blue = transitionLine(lines, 3, 2);

    ASSERT_GT(red, 0);
    ASSERT_GT(green, 0);
    ASSERT_GT(blue, 0);
    EXPECT_EQ(green, red) << "zeleni nije poravnat";
    EXPECT_EQ(blue, red) << "plavi nije poravnat - D3 nije zaobidjen";
}

// --- prekid ---------------------------------------------------------------------

TEST_F(ScanSessionTest, CancellationStopsTheScan) {
    turnLampOn();
    const ScanPlan plan = makePlan(300, image::ColorMode::Color, 8, 64, 50);
    ScanSession session{registers, gate, plan};
    ASSERT_TRUE(session.begin());

    CancellationToken token;
    std::vector<std::uint8_t> line(session.outputBytesPerLine(), 0);

    ASSERT_TRUE(session.nextLine(line, token));
    token.cancel();

    const auto cancelled = session.nextLine(line, token);
    ASSERT_FALSE(cancelled);
    EXPECT_EQ(cancelled.error().code, ErrorCode::Cancelled);
    EXPECT_LT(session.statistics().outputLinesProduced, 50);
}

TEST_F(ScanSessionTest, AbortStopsTheChip) {
    turnLampOn();
    const ScanPlan plan = makePlan(300, image::ColorMode::Color, 8, 64, 50);
    ScanSession session{registers, gate, plan};
    ASSERT_TRUE(session.begin());

    std::vector<std::uint8_t> line(session.outputBytesPerLine(), 0);
    ASSERT_TRUE(session.nextLine(line, CancellationToken::never()));

    ASSERT_TRUE(session.abort());

    rts8822::ScanRegisters check{registers, gate};
    const auto busy = check.isExecuting();
    ASSERT_TRUE(busy);
    EXPECT_FALSE(busy.value());
}

// Sesija koja izadje iz opsega usred prolaza MORA zaustaviti cip.
//
// Bez toga uredjaj nastavlja da skenira i posle poslednjeg reda koji je iko
// procitao - glava se krece, a sledeci prolaz zatice zauzet uredjaj.
TEST_F(ScanSessionTest, DestructorStopsAPassLeftRunning) {
    turnLampOn();
    const ScanPlan plan = makePlan(300, image::ColorMode::Color, 8, 64, 50);

    {
        ScanSession session{registers, gate, plan};
        ASSERT_TRUE(session.begin());

        std::vector<std::uint8_t> line(session.outputBytesPerLine(), 0);
        ASSERT_TRUE(session.nextLine(line, CancellationToken::never()));
        EXPECT_FALSE(session.finished());
        // Ostatak se NE cita; sesija odlazi iz opsega usred prolaza.
    }

    rts8822::ScanRegisters check{registers, gate};
    const auto busy = check.isExecuting();
    ASSERT_TRUE(busy);
    EXPECT_FALSE(busy.value()) << "cip je ostao da skenira";
}

// Ista provera na urednom kraju: kada slika izadje do kraja, prolaz se zatvara
// sam. Sledeci prolaz na istom uredjaju zato mora dati punu sliku.
TEST_F(ScanSessionTest, TwoPassesInARowBothDeliverEveryLine) {
    turnLampOn();
    const ScanPlan plan = makePlan(300, image::ColorMode::Gray, 8, 64, 8);

    for (int pass = 0; pass < 3; ++pass) {
        ScanSession session{registers, gate, plan};
        ASSERT_TRUE(session.begin()) << "prolaz " << pass;
        const auto lines = run(session);
        EXPECT_EQ(lines.size(), 8u) << "prolaz " << pass;
        EXPECT_TRUE(session.finished()) << "prolaz " << pass;
        EXPECT_EQ(session.statistics().unclosedPasses, 0) << "prolaz " << pass;
    }

    rts8822::ScanRegisters check{registers, gate};
    const auto busy = check.isExecuting();
    ASSERT_TRUE(busy);
    EXPECT_FALSE(busy.value());
}

TEST_F(ScanSessionTest, TransportLossSurfacesAsAnError) {
    turnLampOn();
    const ScanPlan plan = makePlan(300, image::ColorMode::Color, 8, 64, 50);
    ScanSession session{registers, gate, plan};
    ASSERT_TRUE(session.begin());

    std::vector<std::uint8_t> line(session.outputBytesPerLine(), 0);
    ASSERT_TRUE(session.nextLine(line, CancellationToken::never()));

    device.faults().injectOnce(sim::TransferKind::BulkRead, ErrorCode::TransportLost);

    const auto lost = session.nextLine(line, CancellationToken::never());
    ASSERT_FALSE(lost);
    EXPECT_EQ(lost.error().code, ErrorCode::TransportLost);
}
