// Smanjivanje slike.
//
// Portovano iz Resize_Decrease (rts8822.c:5719) i iz uspravne grane
// Read_ResizeBlock (rts8822.c:6790). Testovi zato ne mere "izgleda dobro" nego
// tacne celobrojne vrednosti - ako se zaokruzivanje pomeri, port vise ne radi
// isto sto i referenca i to mora da se vidi.
//
// Ovo je jedini nacin da 50, 75, 100 i 200 dpi uopste postoje: skener ih nema.

#include "image/Resize.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <numeric>
#include <vector>

using namespace g2710;
using namespace g2710::image;

namespace {

std::vector<std::uint16_t> resizeRow(const std::vector<std::uint16_t>& from,
                                     int fromResolution, std::size_t toWidth,
                                     int toResolution) {
    std::vector<std::uint16_t> to(toWidth, 0);
    const Status status = resizeLineDown(from, fromResolution, to, toResolution);
    EXPECT_TRUE(status) << (status ? "" : status.error().context);
    return to;
}

// Pakuj bitove najvisi prvi, kao sto cip isporucuje.
std::vector<std::uint8_t> pack(const std::vector<int>& bits) {
    std::vector<std::uint8_t> packed((bits.size() + 7) / 8, 0);
    for (std::size_t i = 0; i < bits.size(); ++i) {
        if (bits[i] != 0) {
            packed[i / 8] |= static_cast<std::uint8_t>(0x80u >> (i % 8));
        }
    }
    return packed;
}

std::vector<int> unpack(const std::vector<std::uint8_t>& packed, std::size_t width) {
    std::vector<int> bits(width, 0);
    for (std::size_t i = 0; i < width; ++i) {
        bits[i] = (packed[i / 8] & (0x80u >> (i % 8))) != 0 ? 1 : 0;
    }
    return bits;
}

}  // namespace

// --- sirina ------------------------------------------------------------------

TEST(ResizedWidth, FollowsTheSameIntegerRatioAsTheCoordinates) {
    EXPECT_EQ(resizedWidth(1200, 150, 100), 800u);
    EXPECT_EQ(resizedWidth(1200, 300, 200), 800u);
    EXPECT_EQ(resizedWidth(1200, 150, 75), 600u);
    EXPECT_EQ(resizedWidth(1200, 150, 50), 400u);
    EXPECT_EQ(resizedWidth(0, 150, 100), 0u);
    EXPECT_EQ(resizedWidth(100, 0, 100), 0u);
}

// --- vodoravno ---------------------------------------------------------------

TEST(ResizeLineDown, HalvingAveragesPairs) {
    const std::vector<std::uint16_t> from{100, 200, 1000, 2000, 30, 70};
    EXPECT_EQ(resizeRow(from, 300, 3, 150),
              (std::vector<std::uint16_t>{150, 1500, 50}));
}

TEST(ResizeLineDown, TwoThirdsSplitsSourcePixelsAcrossOutputs) {
    // 150 -> 100 dpi. Prvi izlaz je 2/3 prvog i 1/3 drugog ulaza,
    // drugi je 1/3 drugog i 2/3 treceg.
    const std::vector<std::uint16_t> from{300, 600, 900};
    const auto to = resizeRow(from, 3, 2, 2);
    ASSERT_EQ(to.size(), 2u);
    EXPECT_EQ(to[0], (300 * 2 + 600 * 1) / 3);
    EXPECT_EQ(to[1], (600 * 1 + 900 * 2) / 3);
}

TEST(ResizeLineDown, UniformInputStaysUniform) {
    const std::vector<std::uint16_t> from(1200, 4321);
    for (int toResolution : {50, 75, 100, 200}) {
        const std::size_t width = resizedWidth(from.size(), 300, toResolution);
        const auto to = resizeRow(from, 300, width, toResolution);
        ASSERT_EQ(to.size(), width) << toResolution;
        for (std::size_t i = 0; i < to.size(); ++i) {
            EXPECT_EQ(to[i], 4321) << toResolution << " piksel " << i;
        }
    }
}

TEST(ResizeLineDown, IdentityWhenResolutionsMatch) {
    std::vector<std::uint16_t> from(64);
    std::iota(from.begin(), from.end(), static_cast<std::uint16_t>(1000));
    EXPECT_EQ(resizeRow(from, 300, from.size(), 300), from);
}

// Referenca ne cita van bafera nego se vrati za jedan piksel; poslednji piksel
// se ponavlja. Trazenje sireg izlaza nego sto odnos daje mora zavrsiti tu,
// a ne u smecu.
TEST(ResizeLineDown, ClampsToTheLastPixelWhenAskedForMoreThanTheRatioGives) {
    const std::vector<std::uint16_t> from{10, 20, 30, 40};
    const auto to = resizeRow(from, 4, 6, 2);
    ASSERT_EQ(to.size(), 6u);
    EXPECT_EQ(to[0], 15);
    EXPECT_EQ(to[1], 35);
    for (std::size_t i = 2; i < to.size(); ++i) {
        EXPECT_EQ(to[i], 40) << "piksel " << i;
    }
}

TEST(ResizeLineDown, PreservesTheOverallAverage) {
    std::vector<std::uint16_t> from(1200);
    for (std::size_t i = 0; i < from.size(); ++i) {
        from[i] = static_cast<std::uint16_t>((i * 53) % 60000);
    }
    const auto to = resizeRow(from, 300, 400, 100);

    const double before = std::accumulate(from.begin(), from.end(), 0.0) / from.size();
    const double after = std::accumulate(to.begin(), to.end(), 0.0) / to.size();
    EXPECT_NEAR(after, before, before * 0.01);
}

TEST(ResizeLineDown, RefusesToEnlarge) {
    const std::vector<std::uint16_t> from{1, 2, 3};
    std::vector<std::uint16_t> to(6, 0);
    const Status status = resizeLineDown(from, 100, to, 200);
    ASSERT_FALSE(status);
    EXPECT_EQ(status.error().code, ErrorCode::InvalidArgument);
}

TEST(ResizeLineDown, RefusesNonsenseArguments) {
    const std::vector<std::uint16_t> from{1, 2, 3};
    std::vector<std::uint16_t> to(2, 0);
    EXPECT_FALSE(resizeLineDown(from, 0, to, 100));
    EXPECT_FALSE(resizeLineDown(from, 100, to, 0));
    EXPECT_FALSE(resizeLineDown({}, 100, to, 50));

    // Prazan izlaz nije greska - nema se sta uraditi.
    EXPECT_TRUE(resizeLineDown(from, 100, {}, 50));
}

// --- lineart -----------------------------------------------------------------

TEST(ResizeLineartDown, AllSetStaysAllSet) {
    const auto from = pack(std::vector<int>(48, 1));
    std::vector<std::uint8_t> to(3, 0);
    ASSERT_TRUE(resizeLineartDown(from, 48, 300, to, 24, 150));
    EXPECT_EQ(unpack(to, 24), std::vector<int>(24, 1));
}

TEST(ResizeLineartDown, AllClearStaysAllClear) {
    const auto from = pack(std::vector<int>(48, 0));
    std::vector<std::uint8_t> to(3, 0xFF);
    ASSERT_TRUE(resizeLineartDown(from, 48, 300, to, 24, 150));
    EXPECT_EQ(unpack(to, 24), std::vector<int>(24, 0));
}

// Prag u referenci je `rescont2 > to_resolution / 2`, a svaki upaljen ulazni
// piksel doprinosi punih `to_resolution`. Posledica: JEDAN upaljen ulazni
// piksel pali izlazni, ma koliko ih je u grupi. To nije previd nego ono sto
// lineart trazi - tanka linija teksta ne sme da nestane pri smanjivanju.
TEST(ResizeLineartDown, AnySetPixelSurvivesTheReduction) {
    std::vector<int> bits(48, 0);
    bits[8] = 1;   // usamljen, prvi u grupi od tri
    bits[19] = 1;  // usamljen, drugi u grupi
    bits[29] = 1;  // usamljen, treci u grupi

    const auto from = pack(bits);
    std::vector<std::uint8_t> to(2, 0);
    ASSERT_TRUE(resizeLineartDown(from, 48, 300, to, 16, 100));

    const auto out = unpack(to, 16);
    EXPECT_EQ(out[2], 1) << "izlazni piksel 2 pokriva ulaze 6..8";
    EXPECT_EQ(out[6], 1) << "izlazni piksel 6 pokriva ulaze 18..20";
    EXPECT_EQ(out[9], 1) << "izlazni piksel 9 pokriva ulaze 27..29";
}

// Naspram istog pravila: izlazni piksel je ugasen samo ako su SVI njegovi
// ulazi ugaseni.
TEST(ResizeLineartDown, OutputIsClearOnlyWhenEveryContributingInputIsClear) {
    std::vector<int> bits(32, 0);
    for (int i = 0; i < 8; ++i) {
        bits[i] = 1;
    }

    const auto from = pack(bits);
    std::vector<std::uint8_t> to(2, 0);
    ASSERT_TRUE(resizeLineartDown(from, 32, 300, to, 16, 150));

    const auto out = unpack(to, 16);
    for (int i = 0; i < 4; ++i) {
        EXPECT_EQ(out[i], 1) << "blok, izlazni piksel " << i;
    }
    for (int i = 4; i < 16; ++i) {
        EXPECT_EQ(out[i], 0) << "prazno, izlazni piksel " << i;
    }
}

TEST(ResizeLineartDown, RejectsBuffersThatAreTooSmall) {
    const auto from = pack(std::vector<int>(32, 1));
    std::vector<std::uint8_t> tiny(1, 0);
    EXPECT_FALSE(resizeLineartDown(from, 32, 300, tiny, 16, 150));

    std::vector<std::uint8_t> to(2, 0);
    EXPECT_FALSE(resizeLineartDown({}, 32, 300, to, 16, 150));
    EXPECT_FALSE(resizeLineartDown(from, 32, 150, to, 16, 300));
}

// Prenos pokrivenosti u sledeci izlazni piksel.
//
// Kada upaljen ulazni piksel padne tacno na granicu izlaznog, referenca njegov
// ostatak (`rescont2 = ... ? rescont : 0`, rts8822.c:5851) prenosi u SLEDECI
// izlazni piksel umesto da ga baci. Bez toga tanka linija koja padne na
// granicu nestaje.
//
// Odnos je 7:5 jer odnosi koje G2710 stvarno koristi - 2:1, 3:1 i 3:2 - taj
// prenos nikada ne cine odlucujucim: doprinosi su umnosci izlazne rezolucije,
// a prag je njena polovina, pa se ishod ne menja. Port se ipak testira ceo,
// ne samo na ono sto ovaj uredjaj trazi.
TEST(ResizeLineartDown, CarriesPartialCoverageIntoTheNextOutputPixel) {
    std::vector<int> bits(7, 0);
    bits[1] = 1;

    const auto from = pack(bits);
    std::vector<std::uint8_t> to(1, 0);
    ASSERT_TRUE(resizeLineartDown(from, 7, 7, to, 5, 5));

    const auto out = unpack(to, 5);
    EXPECT_EQ(out[0], 0) << "sam doprinos ne prelazi prag";
    EXPECT_EQ(out[1], 1) << "ostatak je izgubljen umesto da predje u sledeci piksel";
    EXPECT_EQ(out[2], 0);
    EXPECT_EQ(out[3], 0);
    EXPECT_EQ(out[4], 0);
}

// Posledica prenosa: nijedan upaljen ulazni piksel se ne gubi, i nijedan se ne
// razmnozava.
TEST(ResizeLineartDown, EverySetInputPixelLandsInExactlyOneOutputPixel) {
    for (std::size_t setPixel = 0; setPixel < 7; ++setPixel) {
        std::vector<int> bits(7, 0);
        bits[setPixel] = 1;

        const auto from = pack(bits);
        std::vector<std::uint8_t> to(1, 0);
        ASSERT_TRUE(resizeLineartDown(from, 7, 7, to, 5, 5)) << setPixel;

        const auto out = unpack(to, 5);
        const int count = std::accumulate(out.begin(), out.end(), 0);
        EXPECT_EQ(count, 1) << "ulazni piksel " << setPixel;
    }
}

// --- uspravno ----------------------------------------------------------------

TEST(VerticalResampler, FirstLineNeverProducesOutput) {
    VerticalResampler resampler(300, 150, 4);
    ASSERT_TRUE(resampler.valid());

    const std::vector<std::uint16_t> line{1, 2, 3, 4};
    ASSERT_TRUE(resampler.push(line));
    EXPECT_FALSE(resampler.hasOutput())
        << "nema prethodnog reda sa kojim bi se mesalo";
}

TEST(VerticalResampler, HalvingEmitsOneLinePerTwoInputs) {
    VerticalResampler resampler(300, 150, 2);
    std::vector<std::uint16_t> out(2, 0);

    int emitted = 0;
    for (int line = 0; line < 8; ++line) {
        const std::vector<std::uint16_t> input{static_cast<std::uint16_t>(line * 100),
                                               static_cast<std::uint16_t>(line * 100)};
        ASSERT_TRUE(resampler.push(input)) << line;
        if (resampler.hasOutput()) {
            ASSERT_TRUE(resampler.pop(out));
            ++emitted;
        }
    }
    EXPECT_EQ(emitted, 3) << "prvi ulaz nema par, pa 8 ulaza daje 3 izlaza";
    EXPECT_EQ(resampler.producedLines(), 3);
    EXPECT_EQ(resampler.consumedLines(), 8);
}

TEST(VerticalResampler, UniformInputStaysUniform) {
    VerticalResampler resampler(300, 100, 3);
    std::vector<std::uint16_t> out(3, 0);
    const std::vector<std::uint16_t> input{5000, 5000, 5000};

    for (int line = 0; line < 12; ++line) {
        ASSERT_TRUE(resampler.push(input));
        if (resampler.hasOutput()) {
            ASSERT_TRUE(resampler.pop(out));
            EXPECT_EQ(out, (std::vector<std::uint16_t>{5000, 5000, 5000})) << line;
        }
    }
    EXPECT_GT(resampler.producedLines(), 0);
}

// Izlaz mora biti izmedju dva reda od kojih je nastao - to je sve sto linearno
// mesanje sme da uradi.
TEST(VerticalResampler, OutputLiesBetweenTheTwoSourceLines) {
    VerticalResampler resampler(300, 200, 1);
    std::vector<std::uint16_t> out(1, 0);

    std::uint16_t previous = 0;
    for (int line = 0; line < 10; ++line) {
        const std::uint16_t value = static_cast<std::uint16_t>(1000 + line * 500);
        ASSERT_TRUE(resampler.push(std::vector<std::uint16_t>{value}));
        if (resampler.hasOutput()) {
            ASSERT_TRUE(resampler.pop(out));
            EXPECT_GE(out[0], std::min(previous, value)) << line;
            EXPECT_LE(out[0], std::max(previous, value)) << line;
        }
        previous = value;
    }
}

TEST(VerticalResampler, ExpectedOutputLinesMatchesWhatPushActuallyProduces) {
    for (int toResolution : {50, 75, 100, 150, 200}) {
        VerticalResampler resampler(300, toResolution, 1);
        ASSERT_TRUE(resampler.valid()) << toResolution;

        const std::size_t inputs = 60;
        std::vector<std::uint16_t> out(1, 0);
        for (std::size_t line = 0; line < inputs; ++line) {
            ASSERT_TRUE(resampler.push(std::vector<std::uint16_t>{1234}));
            if (resampler.hasOutput()) {
                ASSERT_TRUE(resampler.pop(out));
            }
        }
        EXPECT_EQ(static_cast<std::size_t>(resampler.producedLines()),
                  VerticalResampler::expectedOutputLines(inputs, 300, toResolution))
            << toResolution << " dpi";
    }
}

TEST(VerticalResampler, RefusesWrongLineLengthAndUnreadOutput) {
    VerticalResampler resampler(300, 150, 4);
    const std::vector<std::uint16_t> good{1, 2, 3, 4};
    const std::vector<std::uint16_t> bad{1, 2, 3};

    EXPECT_FALSE(resampler.push(bad));

    // Prag je strogo `>` (rts8822.c:6884), pa pri odnosu 2:1 izlaz stize tek
    // na trecem redu - na drugom je akumulator tacno jednak izvornoj
    // rezoluciji.
    ASSERT_TRUE(resampler.push(good));
    ASSERT_TRUE(resampler.push(good));
    ASSERT_FALSE(resampler.hasOutput());
    ASSERT_TRUE(resampler.push(good));
    ASSERT_TRUE(resampler.hasOutput());

    // Dok se izlaz ne preuzme, sledeci red bi ga pregazio.
    const Status overwritten = resampler.push(good);
    ASSERT_FALSE(overwritten);
    EXPECT_EQ(overwritten.error().code, ErrorCode::InvalidState);

    std::vector<std::uint16_t> wrongSize(3, 0);
    EXPECT_FALSE(resampler.pop(wrongSize));

    std::vector<std::uint16_t> out(4, 0);
    EXPECT_TRUE(resampler.pop(out));
    EXPECT_FALSE(resampler.hasOutput());
    EXPECT_FALSE(resampler.pop(out)) << "drugi pop bez novog reda";
}

TEST(VerticalResampler, RejectsEnlargingAndEmptyLines) {
    EXPECT_FALSE(VerticalResampler(150, 300, 4).valid());
    EXPECT_FALSE(VerticalResampler(300, 150, 0).valid());
    EXPECT_FALSE(VerticalResampler(0, 150, 4).valid());

    VerticalResampler bad(150, 300, 4);
    const std::vector<std::uint16_t> line{1, 2, 3, 4};
    EXPECT_FALSE(bad.push(line));
}

TEST(VerticalResampler, ResetReturnsItToTheInitialState) {
    VerticalResampler resampler(300, 150, 2);
    const std::vector<std::uint16_t> line{7, 7};
    ASSERT_TRUE(resampler.push(line));
    ASSERT_TRUE(resampler.push(line));
    ASSERT_TRUE(resampler.push(line));
    ASSERT_TRUE(resampler.hasOutput());

    resampler.reset();
    EXPECT_FALSE(resampler.hasOutput());
    EXPECT_EQ(resampler.producedLines(), 0);
    EXPECT_EQ(resampler.consumedLines(), 0);

    ASSERT_TRUE(resampler.push(line));
    EXPECT_FALSE(resampler.hasOutput()) << "posle reset-a prvi red opet nema par";
}
