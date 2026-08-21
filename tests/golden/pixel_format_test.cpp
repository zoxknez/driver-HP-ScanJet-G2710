// Format piksela i pretvaranja.
//
// Duzina reda je izvedena iz rts8822.c:8762 RTS_Setup_Depth; pragovi za
// lineart iz srt_hp3800_platform_get.

#include "G2710Profile.generated.h"
#include "image/PixelFormat.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

using namespace g2710;
using namespace g2710::image;

// --- duzina reda -------------------------------------------------------------

// Sirenje osmobitnog uzorka na punu skalu.
//
// Ponavljanje bajta, ne pomeranje. Sa `value << 8` najsvetliji piksel daje
// 0xFF00 umesto 0xFFFF, pa shading racuna pojacanje vece od jedan i tiho
// posvetli celu sliku - greska od 0.4% koja se u 8 bita ne vidi, a u
// kalibraciji se nagomilava.
TEST(WidenToFullScale, RepeatsTheByteInsteadOfShifting) {
    EXPECT_EQ(widenToFullScale(0x00), 0x0000);
    EXPECT_EQ(widenToFullScale(0xFF), 0xFFFF) << "puna skala mora biti dostizna";
    EXPECT_EQ(widenToFullScale(0x80), 0x8080);
    EXPECT_EQ(widenToFullScale(0x01), 0x0101);
}

// Suzavanje je inverzno do zaokruzivanja: sto se prosiri, mora se vratiti.
TEST(WidenToFullScale, RoundTripsThroughReduceTo8Bit) {
    for (int value = 0; value < 256; ++value) {
        const std::uint16_t wide = widenToFullScale(static_cast<std::uint8_t>(value));
        std::uint8_t narrow = 0;
        reduceTo8Bit(std::span<const std::uint16_t>(&wide, 1),
                     std::span<std::uint8_t>(&narrow, 1));
        EXPECT_EQ(narrow, value) << "vrednost " << value;
    }
}

TEST(LineGeometryTest, ColorAtEightBitsIsThreeBytesPerDot) {
    const auto geometry = computeLineGeometry(ColorMode::Color, 8, 100);
    EXPECT_EQ(geometry.bytesPerLine, 300u);
    EXPECT_EQ(geometry.depthCode, DepthCode::Bits8);
}

TEST(LineGeometryTest, SixteenBitsDoublesTheLine) {
    const auto geometry = computeLineGeometry(ColorMode::Color, 16, 100);
    EXPECT_EQ(geometry.bytesPerLine, 600u);
    EXPECT_EQ(geometry.depthCode, DepthCode::Bits16);
}

TEST(LineGeometryTest, TwelveBitsAlsoTakesTwoBytes) {
    // Referenca ne pakuje 12 bita gusce - i dalje dva bajta po kanalu, samo
    // drugi kod dubine.
    const auto geometry = computeLineGeometry(ColorMode::Color, 12, 100);
    EXPECT_EQ(geometry.bytesPerLine, 600u);
    EXPECT_EQ(geometry.depthCode, DepthCode::Bits12);
}

TEST(LineGeometryTest, GrayIsOneChannelPerDot) {
    EXPECT_EQ(computeLineGeometry(ColorMode::Gray, 8, 100).bytesPerLine, 100u);
    EXPECT_EQ(computeLineGeometry(ColorMode::Gray, 16, 100).bytesPerLine, 200u);
}

TEST(LineGeometryTest, LineartIgnoresDepthAndPacksToBits) {
    // rts8822.c:8782 - (bytes_per_line + 7) / 8, bez obzira na `depth`.
    for (const int depth : {8, 12, 16}) {
        const auto geometry = computeLineGeometry(ColorMode::Lineart, depth, 100);
        EXPECT_EQ(geometry.bytesPerLine, 13u) << "dubina " << depth;
        EXPECT_EQ(geometry.depthCode, DepthCode::Lineart);
    }
}

TEST(LineGeometryTest, LineartRoundsUpPartialBytes) {
    EXPECT_EQ(computeLineGeometry(ColorMode::Lineart, 8, 1).bytesPerLine, 1u);
    EXPECT_EQ(computeLineGeometry(ColorMode::Lineart, 8, 8).bytesPerLine, 1u);
    EXPECT_EQ(computeLineGeometry(ColorMode::Lineart, 8, 9).bytesPerLine, 2u);
}

// --- parni i neparni ---------------------------------------------------------

TEST(EvenOddTest, SplitAndMergeAreInverse) {
    const std::vector<std::uint16_t> original{10, 11, 12, 13, 14, 15};

    std::vector<std::uint16_t> even(3);
    std::vector<std::uint16_t> odd(3);
    ASSERT_TRUE(deinterleaveEvenOdd(original, even, odd).hasValue());

    EXPECT_EQ(even, (std::vector<std::uint16_t>{10, 12, 14}));
    EXPECT_EQ(odd, (std::vector<std::uint16_t>{11, 13, 15}));

    std::vector<std::uint16_t> merged(6);
    ASSERT_TRUE(interleaveEvenOdd(even, odd, merged).hasValue());
    EXPECT_EQ(merged, original);
}

TEST(EvenOddTest, HandlesOddPixelCount) {
    // Neparan broj piksela daje jedan parni vise.
    const std::vector<std::uint16_t> original{1, 2, 3, 4, 5};

    std::vector<std::uint16_t> even(3);
    std::vector<std::uint16_t> odd(2);
    ASSERT_TRUE(deinterleaveEvenOdd(original, even, odd).hasValue());

    EXPECT_EQ(even, (std::vector<std::uint16_t>{1, 3, 5}));
    EXPECT_EQ(odd, (std::vector<std::uint16_t>{2, 4}));

    std::vector<std::uint16_t> merged(5);
    ASSERT_TRUE(interleaveEvenOdd(even, odd, merged).hasValue());
    EXPECT_EQ(merged, original);
}

TEST(EvenOddTest, RejectsMismatchedLengths) {
    const std::vector<std::uint16_t> original{1, 2, 3, 4};
    std::vector<std::uint16_t> even(3);  // pogresno
    std::vector<std::uint16_t> odd(2);

    const Status status = deinterleaveEvenOdd(original, even, odd);
    ASSERT_FALSE(status.hasValue());
    EXPECT_EQ(status.error().code, ErrorCode::InvalidArgument);
}

// --- gamma -------------------------------------------------------------------

TEST(GammaTest, UnityGammaIsIdentity) {
    const auto table = makeGammaTable(1.0);
    ASSERT_EQ(table.size(), kGammaTableSize);

    for (std::size_t i = 0; i < table.size(); ++i) {
        EXPECT_EQ(table[i], i) << "ulaz " << i;
    }
}

TEST(GammaTest, EndpointsAreFixed) {
    // Bez obzira na eksponent, crna ostaje crna i bela bela.
    for (const double gamma : {0.5, 1.0, 1.8, 2.2}) {
        const auto table = makeGammaTable(gamma);
        EXPECT_EQ(table.front(), 0) << "gamma " << gamma;
        EXPECT_EQ(table.back(), 255) << "gamma " << gamma;
    }
}

TEST(GammaTest, HigherGammaBrightensMidtones) {
    const auto neutral = makeGammaTable(1.0);
    const auto bright = makeGammaTable(2.2);

    EXPECT_GT(bright[128], neutral[128]);
}

TEST(GammaTest, TableIsMonotonic) {
    const auto table = makeGammaTable(2.2);
    for (std::size_t i = 1; i < table.size(); ++i) {
        EXPECT_GE(table[i], table[i - 1]) << "pad na ulazu " << i;
    }
}

TEST(GammaTest, ApplyKeepsFullOutputDepth) {
    // Gamma ne sme tiho spustiti izlaz na 8 bita.
    const auto table = makeGammaTable(1.0);
    std::vector<std::uint16_t> line{0x0000, 0x8080, 0xFFFF};

    ASSERT_TRUE(applyGamma(table, line).hasValue());

    EXPECT_EQ(line[0], 0x0000);
    EXPECT_EQ(line[2], 0xFFFF) << "bela je spustena ispod pune skale";
}

TEST(GammaTest, RejectsWrongTableSize) {
    const GammaTable tooSmall(16, 0);
    std::vector<std::uint16_t> line{0};

    const Status status = applyGamma(tooSmall, line);
    ASSERT_FALSE(status.hasValue());
    EXPECT_EQ(status.error().code, ErrorCode::InvalidArgument);
}

// --- sivo --------------------------------------------------------------------

TEST(GrayscaleTest, ReferenceMethodUsesOnlyTheRedChannel) {
    // cfg_sensor_get channel_gray je {CL_RED, 0} - referenca ne mesa kanale.
    ASSERT_EQ(profile::kSensor.channelGray[0], 0) << "profil ocekuje CL_RED";

    constexpr std::size_t pixels = 2;
    const std::vector<std::uint16_t> rgb{
        1000, 2000,      // R
        30000, 40000,    // G
        60000, 50000,    // B
    };

    std::vector<std::uint16_t> gray(pixels);
    ASSERT_TRUE(toGrayscale(rgb, pixels, GrayMethod::RedChannel, gray).hasValue());

    EXPECT_EQ(gray[0], 1000);
    EXPECT_EQ(gray[1], 2000);
}

TEST(GrayscaleTest, LuminanceMixesAllThree) {
    constexpr std::size_t pixels = 1;
    const std::vector<std::uint16_t> rgb{1000, 30000, 60000};

    std::vector<std::uint16_t> gray(pixels);
    ASSERT_TRUE(toGrayscale(rgb, pixels, GrayMethod::Luminance, gray).hasValue());

    const auto expected = static_cast<std::uint16_t>(
        0.299 * 1000 + 0.587 * 30000 + 0.114 * 60000);
    EXPECT_NEAR(gray[0], expected, 1);
    EXPECT_NE(gray[0], 1000) << "luminancija se ponasa kao crveni kanal";
}

TEST(GrayscaleTest, RejectsWrongInputLength) {
    const std::vector<std::uint16_t> rgb{1, 2, 3};
    std::vector<std::uint16_t> gray(2);

    const Status status = toGrayscale(rgb, 2, GrayMethod::RedChannel, gray);
    ASSERT_FALSE(status.hasValue());
    EXPECT_EQ(status.error().code, ErrorCode::InvalidArgument);
}

// --- lineart -----------------------------------------------------------------

namespace {

LineartThreshold platformThreshold() {
    LineartThreshold threshold;
    for (const auto& entry : profile::kPlatformParams) {
        if (entry.option == profile::CalibOption::BINARYTHRESHOLDH) {
            threshold.high = entry.value;
        }
        if (entry.option == profile::CalibOption::BINARYTHRESHOLDL) {
            threshold.low = entry.value;
        }
    }
    return threshold;
}

bool bitAt(const std::vector<std::uint8_t>& packed, std::size_t index) {
    return (packed[index / 8] & (0x80u >> (index % 8))) != 0;
}

}  // namespace

TEST(LineartTest, ThresholdsComeFromThePlatformProfile) {
    const auto threshold = platformThreshold();
    EXPECT_EQ(threshold.high, 100) << "BINARYTHRESHOLDH";
    EXPECT_EQ(threshold.low, 99) << "BINARYTHRESHOLDL";
}

TEST(LineartTest, PacksHighestBitFirst) {
    // Cip isporucuje najvisi bit prvi; obrnut redosled bi dao ogledalnu sliku.
    const std::vector<std::uint16_t> gray{60000, 0, 60000, 0, 0, 0, 0, 0};
    std::vector<std::uint8_t> packed(1);

    ASSERT_TRUE(toLineart(gray, LineartThreshold{}, 16, packed).hasValue());

    EXPECT_TRUE(bitAt(packed, 0));
    EXPECT_FALSE(bitAt(packed, 1));
    EXPECT_TRUE(bitAt(packed, 2));
    EXPECT_EQ(packed[0], 0xA0);
}

TEST(LineartTest, HysteresisHoldsTheDecisionBetweenThresholds) {
    // Dva praga postoje da granicni pikseli ne bi davali sum. Vrednost izmedju
    // njih zadrzava prethodnu odluku umesto da se prevrce.
    LineartThreshold threshold;
    threshold.high = 200;
    threshold.low = 100;

    // Skalirano na 16 bita: high = 200<<8, low = 100<<8. Sredina je izmedju.
    const std::uint16_t above = 220 << 8;
    const std::uint16_t between = 150 << 8;
    const std::uint16_t below = 50 << 8;

    const std::vector<std::uint16_t> risingFirst{above, between, between, between,
                                                 0, 0, 0, 0};
    std::vector<std::uint8_t> packed(1);
    ASSERT_TRUE(toLineart(risingFirst, threshold, 16, packed).hasValue());

    EXPECT_TRUE(bitAt(packed, 0));
    EXPECT_TRUE(bitAt(packed, 1)) << "izmedju pragova odluka nije zadrzana";
    EXPECT_TRUE(bitAt(packed, 2));

    const std::vector<std::uint16_t> fallingFirst{below, between, between, between,
                                                  0, 0, 0, 0};
    ASSERT_TRUE(toLineart(fallingFirst, threshold, 16, packed).hasValue());

    EXPECT_FALSE(bitAt(packed, 0));
    EXPECT_FALSE(bitAt(packed, 1)) << "histereza radi samo u jednom smeru";
}

TEST(LineartTest, RejectsInvertedThresholds) {
    LineartThreshold inverted;
    inverted.high = 10;
    inverted.low = 200;

    const std::vector<std::uint16_t> gray(8, 0);
    std::vector<std::uint8_t> packed(1);

    const Status status = toLineart(gray, inverted, 16, packed);
    ASSERT_FALSE(status.hasValue());
    EXPECT_EQ(status.error().code, ErrorCode::InvalidArgument);
}

TEST(LineartTest, PartialByteIsPaddedNotTruncated) {
    const std::vector<std::uint16_t> gray(9, 60000);
    std::vector<std::uint8_t> packed(2);

    ASSERT_TRUE(toLineart(gray, LineartThreshold{}, 16, packed).hasValue());

    EXPECT_EQ(packed[0], 0xFF);
    EXPECT_EQ(packed[1], 0x80) << "deveti piksel nije u najvisem bitu";
}

// --- dubina ------------------------------------------------------------------

TEST(DepthTest, ReducesWithRoundingNotTruncation) {
    const std::vector<std::uint16_t> in{0, 0x0080, 0x8000, 0xFF7F, 0xFFFF};
    std::vector<std::uint8_t> out(in.size());

    reduceTo8Bit(in, out);

    EXPECT_EQ(out[0], 0);
    EXPECT_EQ(out[2], 128);
    EXPECT_EQ(out[4], 255);

    // Odsecanje bi dalo 254 umesto 255 za vrednost tik ispod pune skale.
    EXPECT_EQ(out[3], 255) << "korisceno je odsecanje umesto zaokruzivanja";
}

TEST(DepthTest, EndpointsMapExactly) {
    const std::vector<std::uint16_t> in{0, 0xFFFF};
    std::vector<std::uint8_t> out(2);

    reduceTo8Bit(in, out);
    EXPECT_EQ(out[0], 0);
    EXPECT_EQ(out[1], 255);
}
