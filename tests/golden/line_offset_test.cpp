// Razmak redova senzora.
//
// Izvedeno iz rts8822.c:8678 RTS_Setup_Line_Distances.
//
// Grupa testova na kraju drzi zakljucan defekt D3: hardverska polja su 6-bitna
// i za G2710 se prelivaju bas na 1200 i 2400 dpi - dvema rezolucijama koje je
// autor reference iskljucio. To je konkretan, proverljiv mehanizam za problem
// koji je do sada bio opisan samo kao "until problems are solved".

#include "G2710Profile.generated.h"
#include "image/LineOffset.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <numeric>
#include <vector>

using namespace g2710;
using namespace g2710::image;

namespace {

// Vrednosti iz profila; ako se raziidju, ceo racun ispod meri nesto drugo.
constexpr int kSensorLineDistance = 64;
constexpr int kSensorEvenOdd = 8;
constexpr int kSensorResolution = 2400;

LineOffsetRegisters offsetsAt(int resolution) {
    const bool highRes = resolution > 1200;
    return computeLineOffsets(kSensorLineDistance, kSensorEvenOdd,
                              kSensorResolution, resolution, highRes);
}

}  // namespace

TEST(LineOffsetProfile, ConstantsMatchTheExtractedProfile) {
    EXPECT_EQ(kSensorLineDistance, profile::kSensor.lineDistance);
    EXPECT_EQ(kSensorEvenOdd, profile::kSensor.evenOddDistance);
    EXPECT_EQ(kSensorResolution, profile::kSensor.resolution);
}

// --- racun ------------------------------------------------------------------

TEST(LineOffsetRegistersTest, ScalesFromSensorResolution) {
    // 64 na 2400 dpi je 8 na 300 dpi.
    const auto offsets = offsetsAt(300);
    EXPECT_EQ(offsets.lineDistance, 8);
    EXPECT_EQ(offsets.doubleLineDistance, 16);
}

TEST(LineOffsetRegistersTest, EvenOddIsUnusedBelowHighResolution) {
    // rts8822.c:8694 - ispod visoke rezolucije myevenodddist je 0.
    EXPECT_EQ(offsetsAt(600).evenOdd, 0);
    EXPECT_EQ(offsetsAt(1200).evenOdd, 0);
    EXPECT_GT(offsetsAt(2400).evenOdd, 0) << "na 2400 dpi even/odd se koristi";
}

TEST(LineOffsetRegistersTest, DerivedFieldsAreSums) {
    const auto offsets = offsetsAt(2400);
    EXPECT_EQ(offsets.lineDistancePlusEvenOdd, offsets.lineDistance + offsets.evenOdd);
    EXPECT_EQ(offsets.doubleLineDistance, offsets.lineDistance * 2);
    EXPECT_EQ(offsets.doublePlusEvenOdd, offsets.lineDistance * 2 + offsets.evenOdd);
}

// --- D3: 6-bitna polja se prelivaju bas na 1200 i 2400 ----------------------

TEST(LineOffsetHardware, FitsUpToSixHundredDpi) {
    for (const int resolution : {100, 150, 200, 300, 600}) {
        EXPECT_TRUE(offsetsAt(resolution).fitsInHardware())
            << resolution << " dpi ne staje u 6 bita, a trebalo bi";
    }
}

TEST(LineOffsetHardware, OverflowsAtTwelveHundredDpi) {
    // Prva rezolucija koju je autor reference iskljucio.
    const auto offsets = offsetsAt(1200);

    EXPECT_EQ(offsets.lineDistance, 32);
    EXPECT_EQ(offsets.doubleLineDistance, 64);
    EXPECT_GT(offsets.doubleLineDistance, kHardwareOffsetMax);
    EXPECT_FALSE(offsets.fitsInHardware());
}

TEST(LineOffsetHardware, OverflowsWorseAtTwentyFourHundredDpi) {
    const auto offsets = offsetsAt(2400);

    EXPECT_EQ(offsets.lineDistance, 64);
    EXPECT_EQ(offsets.doubleLineDistance, 128);
    EXPECT_FALSE(offsets.fitsInHardware());
    EXPECT_GT(offsets.lineDistance, kHardwareOffsetMax)
        << "na 2400 dpi i zeleni kanal ispada iz opsega, ne samo plavi";
}

TEST(LineOffsetHardware, TruncationWouldSilentlyZeroTheBlueOffset) {
    // Referenca upisuje kroz masku 0x3F bez provere. Ovo pokazuje sta bi se
    // dogodilo: pomak plavog kanala postaje NULA, pa plava ravan ostaje
    // nepomerena i slika dobija obojene rubove.
    const auto offsets = offsetsAt(1200);
    const int truncated = offsets.doubleLineDistance & kHardwareOffsetMax;

    EXPECT_EQ(truncated, 0)
        << "odsecanje ne daje nulu - mehanizam D3 je drugaciji nego sto je opisan";
    EXPECT_NE(truncated, offsets.doubleLineDistance);
}

TEST(LineOffsetHardware, SupportIsRefusedNotTruncated) {
    // Nasa implementacija odbija umesto da tiho odsece.
    EXPECT_TRUE(hardwareAlignmentSupported(kSensorLineDistance, kSensorEvenOdd,
                                           kSensorResolution, 600, false));
    EXPECT_FALSE(hardwareAlignmentSupported(kSensorLineDistance, kSensorEvenOdd,
                                            kSensorResolution, 1200, false));
    EXPECT_FALSE(hardwareAlignmentSupported(kSensorLineDistance, kSensorEvenOdd,
                                            kSensorResolution, 2400, true));
}

TEST(LineOffsetHardware, BoundaryIsExactlySixtyThree) {
    // Granica nije priblizna - 63 staje, 64 ne.
    LineOffsetRegisters atLimit;
    atLimit.doubleLineDistance = kHardwareOffsetMax;
    EXPECT_TRUE(atLimit.fitsInHardware());

    LineOffsetRegisters justOver;
    justOver.doubleLineDistance = kHardwareOffsetMax + 1;
    EXPECT_FALSE(justOver.fitsInHardware());
}

// --- koliko redova cip mora skenirati vise ------------------------------------
//
// Ovo je racun koji se lako pomesa sa razmakom redova, a posledica je nema:
// cip skenira upola manje redova nego sto korektor treba da vidi, pa ne izadje
// NIJEDAN izlazni red. Bas to se desilo na 1200 i 2400 dpi, i nasla ga je
// hardverska kvalifikacija, ne testovi - zato ovaj test postoji.

TEST(SoftwareAlignmentPadding, IsDoubleTheDistancePlusOne) {
    // rts8822.c:8722, grana za rezolucije do 1200.
    //
    // 300 dpi: razmak je 8, pa produzenje mora biti 17, ne 8.
    EXPECT_EQ(softwareAlignmentPadding(kSensorLineDistance, kSensorEvenOdd,
                                       kSensorResolution, 300, false, true),
              (64 * 300) * 2 / 2400 + 1);

    EXPECT_EQ(softwareAlignmentPadding(kSensorLineDistance, kSensorEvenOdd,
                                       kSensorResolution, 1200, false, true),
              (64 * 1200) * 2 / 2400 + 1);
}

// Produzenje MORA biti bar koliko korektoru treba da napuni cevovod.
TEST(SoftwareAlignmentPadding, CoversWhatTheCorrectorConsumes) {
    for (int resolution : {150, 300, 600, 1200, 2400}) {
        const bool highResolution = resolution > 1200;
        const LineOffsetRegisters offsets = offsetsAt(resolution);

        const int padding = softwareAlignmentPadding(kSensorLineDistance, kSensorEvenOdd,
                                                     kSensorResolution, resolution,
                                                     highResolution, true);

        LineOffsetCorrector corrector(16, offsets.lineDistance);
        EXPECT_GT(padding, corrector.requiredLookahead())
            << resolution << " dpi: cip skenira " << padding
            << " dodatnih redova, a korektor trazi " << corrector.requiredLookahead();
    }
}

// Iznad 1200 dpi referenca u racun ukljucuje i even/odd razmak.
TEST(SoftwareAlignmentPadding, HighResolutionAddsTheEvenOddDistance) {
    const int low = softwareAlignmentPadding(kSensorLineDistance, kSensorEvenOdd,
                                             kSensorResolution, 2400, false, true);
    const int high = softwareAlignmentPadding(kSensorLineDistance, kSensorEvenOdd,
                                              kSensorResolution, 2400, true, true);
    EXPECT_GT(high, low) << "even/odd razmak nije uracunat";
    EXPECT_EQ(high, ((64 * 2) + 8) * 2400 / 2400 + 1);
}

TEST(SoftwareAlignmentPadding, NeverFallsBelowTwo) {
    // Vrlo niska rezolucija: racun bi dao jedan, referenca podize na dva.
    EXPECT_GE(softwareAlignmentPadding(1, 1, 2400, 1, false, true), 2);
    EXPECT_EQ(softwareAlignmentPadding(0, 0, 2400, 150, false, true), 2);
}

TEST(SoftwareAlignmentPadding, RejectsNonsenseResolutions) {
    EXPECT_EQ(softwareAlignmentPadding(64, 8, 0, 300, false, true), 0);
    EXPECT_EQ(softwareAlignmentPadding(64, 8, 2400, 0, false, true), 0);
}

// --- softversko poravnanje ---------------------------------------------------

namespace {

// Red u kome svaki piksel nosi redni broj reda - tako se posle vidi ODAKLE je
// koji kanal dosao.
std::vector<std::uint16_t> markedLine(std::size_t pixels, std::uint16_t marker) {
    return std::vector<std::uint16_t>(pixels, marker);
}

}  // namespace

TEST(LineOffsetCorrectorTest, NeedsLookaheadBeforeFirstOutput) {
    LineOffsetCorrector corrector{4, 3};
    EXPECT_EQ(corrector.requiredLookahead(), 6);
    EXPECT_FALSE(corrector.hasOutput());
}

TEST(LineOffsetCorrectorTest, AlignsChannelsBackOntoTheSameTargetRow) {
    // Kljucni test. Kanali stizu pomereni; posle poravnanja sva tri moraju
    // poticati sa ISTOG reda mete.
    constexpr std::size_t pixels = 4;
    constexpr int distance = 2;
    LineOffsetCorrector corrector{pixels, distance};

    // Crveni red N vidi metu na redu N; zeleni na N - distance; plavi na
    // N - 2*distance. Marker nosi red METE.
    for (std::uint16_t line = 0; line < 12; ++line) {
        ASSERT_TRUE(corrector.push(0, markedLine(pixels, line)).hasValue());
        ASSERT_TRUE(corrector.push(1, markedLine(pixels,
                        static_cast<std::uint16_t>(line + distance))).hasValue());
        ASSERT_TRUE(corrector.push(2, markedLine(pixels,
                        static_cast<std::uint16_t>(line + distance * 2))).hasValue());
    }

    ASSERT_TRUE(corrector.hasOutput());

    std::vector<std::uint16_t> out(3 * pixels);
    ASSERT_TRUE(corrector.pop(out).hasValue());

    // Sva tri kanala moraju nositi isti marker - isti red mete.
    EXPECT_EQ(out[0], out[pixels]) << "crveni i zeleni nisu sa istog reda";
    EXPECT_EQ(out[pixels], out[2 * pixels]) << "zeleni i plavi nisu sa istog reda";
}

TEST(LineOffsetCorrectorTest, WithoutCorrectionChannelsWouldDisagree) {
    // Kontrola: ista postavka sa razmakom 0 pokazuje da markeri STVARNO
    // odudaraju kada se ne poravnaju. Bez ovoga test iznad ne bi razlikovao
    // ispravno poravnanje od ulaza koji je vec poravnat.
    constexpr std::size_t pixels = 4;
    constexpr int distance = 2;
    LineOffsetCorrector passthrough{pixels, 0};

    for (std::uint16_t line = 0; line < 8; ++line) {
        ASSERT_TRUE(passthrough.push(0, markedLine(pixels, line)).hasValue());
        ASSERT_TRUE(passthrough.push(1, markedLine(pixels,
                        static_cast<std::uint16_t>(line + distance))).hasValue());
        ASSERT_TRUE(passthrough.push(2, markedLine(pixels,
                        static_cast<std::uint16_t>(line + distance * 2))).hasValue());
    }

    std::vector<std::uint16_t> out(3 * pixels);
    ASSERT_TRUE(passthrough.pop(out).hasValue());

    EXPECT_NE(out[0], out[2 * pixels])
        << "kanali se poklapaju i bez poravnanja - test iznad ne dokazuje nista";
}

TEST(LineOffsetCorrectorTest, ProducesSuccessiveTargetRows) {
    constexpr std::size_t pixels = 2;
    constexpr int distance = 1;
    LineOffsetCorrector corrector{pixels, distance};

    for (std::uint16_t line = 0; line < 10; ++line) {
        ASSERT_TRUE(corrector.push(0, markedLine(pixels, line)).hasValue());
        ASSERT_TRUE(corrector.push(1, markedLine(pixels,
                        static_cast<std::uint16_t>(line + distance))).hasValue());
        ASSERT_TRUE(corrector.push(2, markedLine(pixels,
                        static_cast<std::uint16_t>(line + distance * 2))).hasValue());
    }

    std::vector<std::uint16_t> out(3 * pixels);
    std::vector<std::uint16_t> markers;
    while (corrector.hasOutput()) {
        ASSERT_TRUE(corrector.pop(out).hasValue());
        markers.push_back(out[0]);
    }

    ASSERT_GE(markers.size(), 3u);
    for (std::size_t i = 1; i < markers.size(); ++i) {
        EXPECT_EQ(markers[i], markers[i - 1] + 1) << "redovi nisu uzastopni";
    }
}

TEST(LineOffsetCorrectorTest, RejectsWrongLineLength) {
    LineOffsetCorrector corrector{4, 1};
    const std::vector<std::uint16_t> tooShort(3, 0);

    const Status status = corrector.push(0, tooShort);
    ASSERT_FALSE(status.hasValue());
    EXPECT_EQ(status.error().code, ErrorCode::InvalidArgument);
}

TEST(LineOffsetCorrectorTest, RejectsUnknownChannel) {
    LineOffsetCorrector corrector{4, 1};
    const std::vector<std::uint16_t> line(4, 0);

    const Status status = corrector.push(3, line);
    ASSERT_FALSE(status.hasValue());
    EXPECT_EQ(status.error().code, ErrorCode::InvalidArgument);
}

TEST(LineOffsetCorrectorTest, PopWithoutOutputIsRefused) {
    LineOffsetCorrector corrector{4, 2};
    std::vector<std::uint16_t> out(12);

    const Status status = corrector.pop(out);
    ASSERT_FALSE(status.hasValue());
    EXPECT_EQ(status.error().code, ErrorCode::InvalidState);
}

TEST(LineOffsetCorrectorTest, ZeroDistanceIsPassthrough) {
    // Na niskim rezolucijama razmak moze ispasti nula; tada korektor ne sme
    // traziti nikakav lookahead.
    constexpr std::size_t pixels = 2;
    LineOffsetCorrector corrector{pixels, 0};

    EXPECT_EQ(corrector.requiredLookahead(), 0);
    for (std::size_t channel = 0; channel < kChannels; ++channel) {
        ASSERT_TRUE(corrector.push(channel, markedLine(pixels, 7)).hasValue());
    }
    EXPECT_TRUE(corrector.hasOutput());
}
