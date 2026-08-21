// Pun prolaz akvizicije protiv simulatora.
//
// Ovde se prvi put spaja sve: registri geometrije, pokretanje, bulk kanal,
// virtuelni motor i virtuelni CCD. Ono sto se testira nije "izlazi nesto"
// nego da li izlazi ONO sto bi izaslo iz cipa.
//
// Najvazniji deo je poslednja grupa. Simulator NE dobija poravnanje kanala
// pozivom - cita ga iz istih sestobitnih polja u koja ga upisujemo. Zato na
// 300 dpi kanali izlaze poravnati, a na 1200 dpi plavi kasni: polje se
// odseklo. To je D3, reprodukovan kroz stvarne registre umesto pretpostavljen.

#include "SimTransport.h"
#include "image/LineOffset.h"
#include "rts8822/Lamp.h"
#include "rts8822/RegisterFile.h"
#include "rts8822/Registers.h"
#include "rts8822/ScanRegisters.h"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

using namespace g2710;
using namespace g2710::rts8822;

namespace {

constexpr int kSensorResolution = 2400;
constexpr int kMotorResolution = 1200;

// Granica bele i crne trake na test-meti, u koracima motora.
// target::kWhiteStripBottom je 5 mm.
constexpr int kStripBoundarySteps = static_cast<int>((5.0 / 25.4) * kMotorResolution);

struct PassSetup {
    int resolution = 300;
    int channels = 3;
    int width = 64;
    int lines = 30;
    int startPosition = 0;
    bool clearOffsets = false;
};

class ScanAcquisition : public ::testing::Test {
protected:
    sim::SimTransport device;
    RegisterFile registers{device};
    ManualClock clock;
    ScanRegisters scan{registers, SafetyGate{SafetyLevel::FullScan}};

    void SetUp() override {
        // Savrsen senzor: ovde se meri geometrija i poravnanje, ne kalibracija.
        device.ccd().makeIdeal();

        // Lampa se pali KROZ REGISTRE, kao u radu.
        //
        // Dva koraka, ne jedan: upaljena lampa bez postavljenog PWM duty
        // cycle-a daje nivo nula, pa bi cela slika bila crna. Simulator to
        // trazi zato sto to trazi i uredjaj - Lamp_PWM_Setup nije opcion.
        Lamp lamp{registers, SafetyGate{SafetyLevel::FullScan}, clock};
        EXPECT_TRUE(lamp.setLamp(LampKind::Flatbed, true));
        EXPECT_TRUE(lamp.setupPwm(LampKind::Flatbed));
        device.advanceTime(60000);
    }

    // Konfigurisi i pokreni prolaz. Vraca ocekivanu duzinu reda u bajtovima.
    std::size_t start(const PassSetup& pass) {
        device.motor().teleportTo(pass.startPosition);

        const int ratio = kSensorResolution / pass.resolution;
        EXPECT_TRUE(scan.setResolutionRatio(ratio));
        EXPECT_TRUE(scan.setDummyLine(1));

        CoordinateScaling scaling;
        scaling.resolutionRatio = ratio;
        scaling.dummyLine = 1;

        const ScanGeometry pixels{1, 1, pass.width, pass.lines};
        EXPECT_TRUE(scan.setGeometry(toRegisterCoordinates(pixels, scaling)));

        ScanFormat format;
        format.channelsPerDot = pass.channels;
        format.depthCode = image::DepthCode::Bits8;
        EXPECT_TRUE(scan.setFormat(format));

        if (pass.clearOffsets) {
            EXPECT_TRUE(scan.clearLineOffsets());
        } else {
            // Iste vrednosti koje planer racuna - i isto tiho odsecanje.
            EXPECT_TRUE(scan.setLineOffsets(image::computeLineOffsets(
                64, 8, kSensorResolution, pass.resolution, pass.resolution > 1200)));
        }

        EXPECT_TRUE(scan.execute());
        return static_cast<std::size_t>(pass.width * pass.channels);
    }

    // Isprazni ceo prolaz kroz bulk kanal.
    std::vector<std::uint8_t> drain() {
        std::vector<std::uint8_t> image;
        std::array<std::byte, 512> chunk{};
        for (;;) {
            auto read = device.bulkRead(chunk);
            EXPECT_TRUE(read);
            if (!read || read.value() == 0) {
                break;
            }
            for (std::size_t i = 0; i < read.value(); ++i) {
                image.push_back(static_cast<std::uint8_t>(chunk[i]));
            }
        }
        return image;
    }

    // Prvi izlazni red u kome dati kanal padne ispod pola. Meri gde kanal
    // vidi prelaz sa bele na crnu traku.
    int transitionLine(const std::vector<std::uint8_t>& image, std::size_t bytesPerLine,
                       int channels, int channel) const {
        const std::size_t lines = image.size() / bytesPerLine;
        for (std::size_t line = 0; line < lines; ++line) {
            // Uzorak iz sredine reda, da ivice ne smetaju.
            const std::size_t pixel = (bytesPerLine / channels) / 2;
            const std::size_t index =
                line * bytesPerLine + pixel * channels + static_cast<std::size_t>(channel);
            if (index < image.size() && image[index] < 0x80) {
                return static_cast<int>(line);
            }
        }
        return -1;
    }
};

}  // namespace

// --- osnovni prolaz -----------------------------------------------------------

TEST_F(ScanAcquisition, NothingComesOutBeforeExecute) {
    std::array<std::byte, 64> buffer{};
    auto read = device.bulkRead(buffer);
    ASSERT_TRUE(read);
    EXPECT_EQ(read.value(), 0u) << "bulk kanal nosi sliku tek kad prolaz krene";
}

TEST_F(ScanAcquisition, DeliversExactlyBytesPerLineTimesLines) {
    PassSetup pass;
    pass.width = 64;
    pass.lines = 20;
    const std::size_t bytesPerLine = start(pass);

    const auto image = drain();
    EXPECT_EQ(image.size(), bytesPerLine * static_cast<std::size_t>(pass.lines));
}

TEST_F(ScanAcquisition, GrayDeliversOneThirdOfColor) {
    PassSetup color;
    color.channels = 3;
    const std::size_t colorLine = start(color);
    const std::size_t colorBytes = drain().size();

    PassSetup gray;
    gray.channels = 1;
    const std::size_t grayLine = start(gray);
    const std::size_t grayBytes = drain().size();

    EXPECT_EQ(colorLine, grayLine * 3);
    EXPECT_EQ(colorBytes, grayBytes * 3);
}

TEST_F(ScanAcquisition, SixteenBitDoublesTheLine) {
    PassSetup pass;
    const std::size_t eightBit = start(pass);
    const std::size_t eightBytes = drain().size();

    device.motor().teleportTo(pass.startPosition);
    ASSERT_TRUE(scan.setFormat({3, image::DepthCode::Bits16, {}}));
    device.pokeRegister(reg::kChannelSize, reg::kChannelSizeWideBit);
    ASSERT_TRUE(scan.execute());
    const std::size_t wideBytes = drain().size();

    EXPECT_EQ(wideBytes, eightBytes * 2) << eightBit;
}

TEST_F(ScanAcquisition, ExecutingBitDropsWhenTheLastLineIsOut) {
    start({});

    const auto busy = scan.isExecuting();
    ASSERT_TRUE(busy);
    ASSERT_TRUE(busy.value());

    drain();

    const auto done = scan.isExecuting();
    ASSERT_TRUE(done);
    EXPECT_FALSE(done.value());
    EXPECT_TRUE(scan.waitScanEnd(std::chrono::milliseconds{0}));
}

// Glava se pomera po JEDAN red - ne stoji, i ne juri.
TEST_F(ScanAcquisition, MotorAdvancesOneLineWorthPerDeliveredLine) {
    PassSetup pass;
    pass.resolution = 300;
    pass.lines = 20;
    pass.startPosition = 100;
    start(pass);

    const int before = device.motor().position();
    drain();
    const int after = device.motor().position();

    // 300 dpi na motoru od 1200 dpi = cetiri koraka po redu.
    EXPECT_EQ(after - before, 4 * pass.lines);
}

TEST_F(ScanAcquisition, HigherResolutionMovesTheHeadLess) {
    PassSetup coarse;
    coarse.resolution = 150;
    coarse.lines = 10;
    coarse.startPosition = 50;
    start(coarse);
    const int coarseBefore = device.motor().position();
    drain();
    const int coarseMoved = device.motor().position() - coarseBefore;

    PassSetup fine;
    fine.resolution = 600;
    fine.lines = 10;
    fine.startPosition = 50;
    start(fine);
    const int fineBefore = device.motor().position();
    drain();
    const int fineMoved = device.motor().position() - fineBefore;

    EXPECT_EQ(coarseMoved, 8 * 10);
    EXPECT_EQ(fineMoved, 2 * 10);
}

// --- sadrzaj slike ------------------------------------------------------------

TEST_F(ScanAcquisition, WhiteStripReadsBrightAndBlackStripReadsDark) {
    PassSetup pass;
    pass.resolution = 300;
    pass.lines = 8;
    pass.startPosition = 24;  // unutar bele trake
    const std::size_t bytesPerLine = start(pass);
    const auto white = drain();
    ASSERT_FALSE(white.empty());

    pass.startPosition = kStripBoundarySteps + 24;  // unutar crne trake
    start(pass);
    const auto black = drain();
    ASSERT_FALSE(black.empty());

    // Vrednosti nisu proizvoljne: refleksija trake puta nivo lampe, pa
    // suzeno na osam bita. Bela je 0.95 * 52000 = 49400 -> 192, crna je
    // 0.03 * 52000 = 1560 -> 6.
    const std::size_t sample = bytesPerLine / 2;
    EXPECT_NEAR(white[sample], 192, 3) << "bela traka";
    EXPECT_NEAR(black[sample], 6, 3) << "crna traka";
    EXPECT_GT(white[sample] - black[sample], 150) << "trake se moraju jasno razlikovati";
}

TEST_F(ScanAcquisition, EveryLineOfAUniformStripLooksTheSame) {
    PassSetup pass;
    pass.resolution = 300;
    pass.lines = 12;
    pass.startPosition = 24;
    const std::size_t bytesPerLine = start(pass);

    const auto image = drain();
    ASSERT_EQ(image.size(), bytesPerLine * 12);

    for (std::size_t line = 1; line < 12; ++line) {
        for (std::size_t i = 0; i < bytesPerLine; ++i) {
            ASSERT_EQ(image[line * bytesPerLine + i], image[i])
                << "red " << line << ", bajt " << i;
        }
    }
}

// --- poravnanje kanala, i D3 ---------------------------------------------------

// Do 600 dpi sva tri polja staju u sest bita, pa cip poravna kanale sam i sve
// tri boje vide prelaz u ISTOM redu.
TEST_F(ScanAcquisition, ChannelsAreAlignedAtThreeHundredDpi) {
    PassSetup pass;
    pass.resolution = 300;
    pass.lines = 40;
    pass.startPosition = kStripBoundarySteps - 40;
    const std::size_t bytesPerLine = start(pass);

    const auto image = drain();
    ASSERT_FALSE(image.empty());

    const int red = transitionLine(image, bytesPerLine, 3, 0);
    const int green = transitionLine(image, bytesPerLine, 3, 1);
    const int blue = transitionLine(image, bytesPerLine, 3, 2);

    ASSERT_GT(red, 0) << "prelaz mora biti unutar prozora";
    EXPECT_EQ(green, red) << "zeleni kasni";
    EXPECT_EQ(blue, red) << "plavi kasni";
}

// Na 1200 dpi polje za plavi kanal trazi 64, a sest bita nosi najvise 63.
// Odsecanje na nulu znaci da cip plavi kanal NE pomera - i to se vidi kao
// pomeren prelaz, tacno onoliko koliko iznosi dvostruki razmak redova.
TEST_F(ScanAcquisition, BlueChannelIsMisalignedAtTwelveHundredDpi) {
    PassSetup pass;
    pass.resolution = 1200;
    pass.lines = 200;
    pass.startPosition = kStripBoundarySteps - 150;
    const std::size_t bytesPerLine = start(pass);

    // Polje se stvarno odseklo - to je preduslov za sve ostalo.
    const auto offsets = scan.lineOffsets();
    ASSERT_TRUE(offsets);
    EXPECT_EQ(offsets.value().lineDistance, 32) << "zeleni jos staje";
    EXPECT_EQ(offsets.value().doubleLineDistance, 0) << "plavi je odsecen";

    const auto image = drain();
    ASSERT_FALSE(image.empty());

    const int red = transitionLine(image, bytesPerLine, 3, 0);
    const int green = transitionLine(image, bytesPerLine, 3, 1);
    const int blue = transitionLine(image, bytesPerLine, 3, 2);

    ASSERT_GT(red, 0);
    EXPECT_EQ(green, red) << "zeleni je i dalje poravnat";

    ASSERT_GT(blue, 0);
    EXPECT_LT(blue, red) << "plavi mora ispasti pomeren";
    EXPECT_EQ(red - blue, 64)
        << "pomak je tacno dvostruki razmak redova, izrazen u redovima na 1200 dpi";
}

// Kada se hardversko poravnanje ugasi, POMERENA su oba kanala - to je grana
// FIX_BY_SOFT, u kojoj poravnanje preuzima LineOffsetCorrector.
TEST_F(ScanAcquisition, ClearingOffsetsLeavesEveryChannelWhereTheSensorSeesIt) {
    PassSetup pass;
    pass.resolution = 300;
    pass.lines = 60;
    pass.startPosition = kStripBoundarySteps - 120;
    pass.clearOffsets = true;
    const std::size_t bytesPerLine = start(pass);

    const auto image = drain();
    ASSERT_FALSE(image.empty());

    const int red = transitionLine(image, bytesPerLine, 3, 0);
    const int green = transitionLine(image, bytesPerLine, 3, 1);
    const int blue = transitionLine(image, bytesPerLine, 3, 2);

    ASSERT_GT(red, 0);
    ASSERT_GT(green, 0);
    ASSERT_GT(blue, 0);

    // Razmak je 32 koraka motora po kanalu, a red na 300 dpi nosi 4 koraka.
    EXPECT_EQ(red - green, 8);
    EXPECT_EQ(red - blue, 16);
}

// --- odbijanja -----------------------------------------------------------------

TEST_F(ScanAcquisition, ConfigurationTheChipCannotReadStartsNothing) {
    // Bez odnosa rezolucija koordinate nemaju smisla.
    device.pokeRegister(reg::kResolutionRatio, 0);
    ASSERT_TRUE(scan.setDummyLine(1));
    ASSERT_TRUE(scan.setGeometry({1, 1, 64, 10}));
    ASSERT_TRUE(scan.setFormat({3, image::DepthCode::Bits8, {}}));
    ASSERT_TRUE(scan.execute());

    const auto busy = scan.isExecuting();
    ASSERT_TRUE(busy);
    EXPECT_FALSE(busy.value());
    EXPECT_TRUE(drain().empty());
}

TEST_F(ScanAcquisition, WarmResetStopsAPassInFlight) {
    start({});

    std::array<std::byte, 64> chunk{};
    ASSERT_TRUE(device.bulkRead(chunk));

    ASSERT_TRUE(scan.warmReset());

    const auto busy = scan.isExecuting();
    ASSERT_TRUE(busy);
    EXPECT_FALSE(busy.value());
    EXPECT_TRUE(drain().empty()) << "posle reseta se nista vise ne isporucuje";
}
