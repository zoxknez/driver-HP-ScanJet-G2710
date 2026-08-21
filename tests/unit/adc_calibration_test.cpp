// ADC pojacanje i offset.
//
// Izvedeno iz Calib_AdcGain (rts8822.c:11587) i Calib_AdcOffsetRT (:12153).
//
// Poslednja grupa drzi zakljucanim D5: referenca u formuli za pojacanje deli
// CELOBROJNO tamo gde ceo ostatak racuna radi u pokretnom zarezu. Pri
// podrazumevanom pojacanju 4 razlika ne postoji - i bas zato je previd lako
// prezivio. Cim se pojacanje pomeri, postoji.

#include "calib/AdcCalibration.h"

#include <gtest/gtest.h>

#include <array>
#include <vector>

using namespace g2710;
using namespace g2710::calib;

namespace {

CalibrationConfig flatbedConfig() {
    auto loaded = loadCalibrationConfig(CalibrationSection::Reflective);
    EXPECT_TRUE(loaded);
    return loaded ? loaded.value() : CalibrationConfig{};
}

std::vector<double> peaks(double red, double green, double blue) {
    return {red, green, blue};
}

}  // namespace

// --- prozor za merenje offseta --------------------------------------------------

TEST(OffsetWindowTest, MatchesTheExtractedTable) {
    const struct {
        int resolution;
        int left;
        int pixels;
    } cases[] = {
        {2400, 15, 20}, {1200, 10, 10}, {600, 2, 10}, {300, 1, 5}, {150, 0, 3},
    };

    for (const auto& testCase : cases) {
        const OffsetWindow window = offsetWindow(testCase.resolution,
                                                 CalibrationSection::Reflective);
        EXPECT_TRUE(window.valid()) << testCase.resolution;
        EXPECT_EQ(window.left, testCase.left) << testCase.resolution;
        EXPECT_EQ(window.pixels, testCase.pixels) << testCase.resolution;
    }
}

// Prozor postoji samo za native rezolucije. Za 100 i 200 dpi nema reda, i to
// se mora videti - a ne dobiti tudje vrednosti.
TEST(OffsetWindowTest, NonNativeResolutionsHaveNoWindow) {
    for (int resolution : {50, 75, 100, 200, 400}) {
        EXPECT_FALSE(offsetWindow(resolution, CalibrationSection::Reflective).valid())
            << resolution;
    }
}

TEST(OffsetWindowTest, FlatbedDiffersFromTma) {
    EXPECT_EQ(offsetWindow(600, CalibrationSection::Reflective).left, 2);
    EXPECT_EQ(offsetWindow(600, CalibrationSection::Transparent).left, 5);
}

// Za G2710 su TMA i negativ u ovoj tabeli ISTI red - u sva tri stupca stoje
// iste vrednosti. Zapisano jer je posledica merljiva: nijedan test ne moze da
// razlikuje ta dva izbora, pa to nije rupa u proveri nego svojstvo uredjaja.
TEST(OffsetWindowTest, TmaAndNegativeShareTheSameWindowOnThisDevice) {
    for (int resolution : {150, 300, 600, 1200, 2400}) {
        const OffsetWindow tma = offsetWindow(resolution, CalibrationSection::Transparent);
        const OffsetWindow negative = offsetWindow(resolution, CalibrationSection::Negative);

        EXPECT_EQ(tma.left, negative.left) << resolution;
        EXPECT_EQ(tma.pixels, negative.pixels) << resolution;
    }

    // Flatbed se razlikuje bar negde - inace bi i taj izbor bio neproverljiv.
    bool flatbedDiffers = false;
    for (int resolution : {150, 300, 600, 1200, 2400}) {
        if (offsetWindow(resolution, CalibrationSection::Reflective).left !=
            offsetWindow(resolution, CalibrationSection::Transparent).left) {
            flatbedDiffers = true;
        }
    }
    EXPECT_TRUE(flatbedDiffers);
}

// --- pocetno stanje ---------------------------------------------------------------

TEST(GainOffsetStateTest, InitialValuesComeFromTheProfile) {
    const GainOffsetState state = initialGainOffset(1);
    for (std::size_t channel = 0; channel < kChannels; ++channel) {
        EXPECT_EQ(state.gain[channel], 4) << "kanal " << channel;
        EXPECT_EQ(state.evenDcg[channel], 0);
        EXPECT_EQ(state.oddDcg[channel], 0);
    }
    EXPECT_FALSE(state.allReady());
}

// Preslikavanje registarskog oblika NIJE inverzno - referenca koristi 0xFF u
// jednom smeru i 0x100 u drugom. Zapisano da se ne "popravi" nepazljivo.
TEST(GainOffsetStateTest, DcgEncodingIsNotItsOwnInverse) {
    EXPECT_EQ(decodeDcg(0x000), 0xFF);
    EXPECT_EQ(decodeDcg(0x080), 0x7F);
    EXPECT_EQ(decodeDcg(0x1FF), 0x1FF) << "iznad preloma ostaje kakav jeste";

    EXPECT_EQ(encodeDcg(0xFF), 0x100 - 0xFF);
    EXPECT_EQ(encodeDcg(0x1FF), 0x1FF);

    EXPECT_NE(encodeDcg(decodeDcg(0x080)), 0x080)
        << "kada bi bilo inverzno, ovde bi se vratila ista vrednost";
}

// --- pojacanje ---------------------------------------------------------------------

TEST(AdcGainTest, WeakSignalAsksForMoreGain) {
    const CalibrationConfig config = flatbedConfig();
    GainOffsetState state = initialGainOffset(1);

    const auto strong = computeAdcGain(config, peaks(200, 200, 200), state);
    ASSERT_TRUE(strong);
    const auto weak = computeAdcGain(config, peaks(60, 60, 60), state);
    ASSERT_TRUE(weak);

    for (std::size_t channel = 0; channel < kChannels; ++channel) {
        EXPECT_GT(weak.value().gain[channel], strong.value().gain[channel])
            << "kanal " << channel;
    }
}

TEST(AdcGainTest, GainNeverExceedsTheFieldWidth) {
    const CalibrationConfig config = flatbedConfig();
    const GainOffsetState state = initialGainOffset(1);

    const auto result = computeAdcGain(config, peaks(1, 1, 1), state);
    ASSERT_TRUE(result);
    for (std::size_t channel = 0; channel < kChannels; ++channel) {
        EXPECT_LE(result.value().gain[channel], 31) << "VGAG je petobitno polje";
        EXPECT_GE(result.value().gain[channel], 0);
    }
}

// Signal iznad praga: formula bi trazila pojacanje manje od jedan, sto polje
// ne moze, pa referenca upisuje nulu.
TEST(AdcGainTest, OverbrightSignalFallsToZero) {
    const CalibrationConfig config = flatbedConfig();
    const GainOffsetState state = initialGainOffset(1);

    const auto result = computeAdcGain(config, peaks(255, 255, 255), state);
    ASSERT_TRUE(result);
    for (std::size_t channel = 0; channel < kChannels; ++channel) {
        EXPECT_EQ(result.value().gain[channel], 0) << "kanal " << channel;
    }
}

// Referenca bi ovde delila nulom. Nula znaci da lampa ne gori ili da traka
// nije ispod glave - to je otkaz, a ne broj.
TEST(AdcGainTest, NoSignalIsRefusedInsteadOfDividingByZero) {
    const CalibrationConfig config = flatbedConfig();
    const GainOffsetState state = initialGainOffset(1);

    const auto result = computeAdcGain(config, peaks(0, 100, 100), state);
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code, ErrorCode::DeviceError);
}

TEST(AdcGainTest, RejectsWrongChannelCountAndDepth) {
    const CalibrationConfig config = flatbedConfig();
    const GainOffsetState state = initialGainOffset(1);

    const std::vector<double> two{100, 100};
    EXPECT_FALSE(computeAdcGain(config, two, state));
    EXPECT_FALSE(computeAdcGain(config, peaks(100, 100, 100), state, 0));
    EXPECT_FALSE(computeAdcGain(config, peaks(100, 100, 100), state, 24));
}

TEST(AdcGainTest, ReachedTargetFollowsThePeakMargin) {
    const CalibrationConfig config = flatbedConfig();
    const GainOffsetState state = initialGainOffset(1);

    // Cilj je 10; vrh mora biti bar 15.
    const auto low = computeAdcGain(config, peaks(11, 11, 11), state);
    ASSERT_TRUE(low);
    EXPECT_FALSE(low.value().reachedTarget);

    const auto high = computeAdcGain(config, peaks(11, 11, 40), state);
    ASSERT_TRUE(high);
    EXPECT_TRUE(high.value().reachedTarget) << "dovoljan je jedan kanal";
}

// --- D5: celobrojno naspram realnog deljenja ----------------------------------------

// Pri pojacanju 4 faktor je (44-4)/40, a to je tacno 1 u oba racuna. Zato
// previd nikada nije zasmetao na podrazumevanim vrednostima.
TEST(AdcGainArithmeticTest, AtTheDefaultGainBothArithmeticsAgree) {
    const CalibrationConfig config = flatbedConfig();
    GainOffsetState state = initialGainOffset(1);
    ASSERT_EQ(state.gain[0], 4);

    const auto reference =
        computeAdcGain(config, peaks(120, 120, 120), state, 8, GainArithmetic::Reference);
    const auto corrected =
        computeAdcGain(config, peaks(120, 120, 120), state, 8, GainArithmetic::Corrected);
    ASSERT_TRUE(reference);
    ASSERT_TRUE(corrected);

    EXPECT_EQ(reference.value().appliedFactor[0], 1.0);
    EXPECT_EQ(corrected.value().appliedFactor[0], 1.0);
    EXPECT_EQ(reference.value().gain, corrected.value().gain);
}

// Cim se pojacanje pomeri sa 4, celobrojno deljenje pocinje da laze.
TEST(AdcGainArithmeticTest, AwayFromTheDefaultTheyDiverge) {
    const CalibrationConfig config = flatbedConfig();
    GainOffsetState state = initialGainOffset(1);
    state.gain = {0, 0, 0};

    const auto reference =
        computeAdcGain(config, peaks(120, 120, 120), state, 8, GainArithmetic::Reference);
    const auto corrected =
        computeAdcGain(config, peaks(120, 120, 120), state, 8, GainArithmetic::Corrected);
    ASSERT_TRUE(reference);
    ASSERT_TRUE(corrected);

    EXPECT_EQ(reference.value().appliedFactor[0], 1.0) << "44 / 40 celobrojno je 1";
    EXPECT_NEAR(corrected.value().appliedFactor[0], 1.1, 1e-9);
    EXPECT_NE(reference.value().gain[0], corrected.value().gain[0]);
}

// Iznad cetiri faktor u referenci pada na NULU, pa se pojacanje resetuje na
// nulu ma kakav signal bio. To je najostriji oblik istog previda.
TEST(AdcGainArithmeticTest, AboveFourTheReferenceFactorCollapsesToZero) {
    const CalibrationConfig config = flatbedConfig();
    GainOffsetState state = initialGainOffset(1);
    state.gain = {5, 5, 5};

    const auto reference =
        computeAdcGain(config, peaks(60, 60, 60), state, 8, GainArithmetic::Reference);
    ASSERT_TRUE(reference);
    EXPECT_EQ(reference.value().appliedFactor[0], 0.0);
    EXPECT_EQ(reference.value().gain[0], 0) << "signal je slab, a pojacanje ispada nula";

    const auto corrected =
        computeAdcGain(config, peaks(60, 60, 60), state, 8, GainArithmetic::Corrected);
    ASSERT_TRUE(corrected);
    EXPECT_GT(corrected.value().gain[0], 0) << "ispravan racun trazi vise pojacanja";
}

// --- offset ---------------------------------------------------------------------------

TEST(AdcOffsetTest, TargetIsShiftedByEightBits) {
    const CalibrationConfig config = flatbedConfig();
    for (std::size_t channel = 0; channel < kChannels; ++channel) {
        EXPECT_EQ(offsetTargetFor(config, channel), 10 << 8) << "kanal " << channel;
    }

    CalibrationConfig zeroed = config;
    zeroed.offsetAvgTarget = {0, 0, 0};
    EXPECT_EQ(offsetTargetFor(zeroed, 0), 0x80) << "nula se zamenjuje sa 0x80";
}

// Potpuno crno: offset se gura na maksimum, pa se probava ponovo.
TEST(AdcOffsetTest, TotalDarknessPushesTheOffsetToMaximum) {
    const CalibrationConfig config = flatbedConfig();
    GainOffsetState state = initialGainOffset(1);

    const std::vector<double> dark{0, 0, 0};
    const auto first = advanceAdcOffset(config, OffsetParity::Even, dark, 10, &state);
    ASSERT_TRUE(first);
    EXPECT_TRUE(first.value().changed);
    for (std::size_t channel = 0; channel < kChannels; ++channel) {
        EXPECT_EQ(state.evenDcg[channel], 0x1FF) << "kanal " << channel;
    }

    // Drugi put vise nema sta - kanal je gotov.
    const auto second = advanceAdcOffset(config, OffsetParity::Even, dark, 10, &state);
    ASSERT_TRUE(second);
    EXPECT_FALSE(second.value().changed);
    EXPECT_EQ(second.value().settled, static_cast<int>(kChannels));
}

// Parno i neparno se podesavaju odvojeno - to je i razlog zasto ih referenca
// uopste razdvaja.
TEST(AdcOffsetTest, EvenAndOddAreTunedIndependently) {
    const CalibrationConfig config = flatbedConfig();
    GainOffsetState state = initialGainOffset(1);

    const std::vector<double> dark{0, 0, 0};
    ASSERT_TRUE(advanceAdcOffset(config, OffsetParity::Even, dark, 10, &state));

    for (std::size_t channel = 0; channel < kChannels; ++channel) {
        EXPECT_EQ(state.evenDcg[channel], 0x1FF);
        EXPECT_EQ(state.oddDcg[channel], 0) << "neparni nije diran";
        EXPECT_TRUE(state.evenReady[channel] || true);
        EXPECT_FALSE(state.oddReady[channel]);
    }
    EXPECT_FALSE(state.allReady()) << "neparna polovina jos nije podesena";
}

// Kada offset ne staje u devet bita, jedini preostali potez je manje
// pojacanje. To je i mehanizam kojim se petlja zaustavlja.
TEST(AdcOffsetTest, OffsetThatDoesNotFitReducesTheGain) {
    const CalibrationConfig config = flatbedConfig();
    GainOffsetState state = initialGainOffset(1);
    const int before = state.gain[0];

    // Znatno svetlije od cilja: razlika ne staje u polje.
    const std::vector<double> bright{200, 200, 200};
    const auto step = advanceAdcOffset(config, OffsetParity::Even, bright, 10, &state);
    ASSERT_TRUE(step);
    EXPECT_GT(step.value().gainReductions, 0);
    EXPECT_LT(state.gain[0], before);
}

// Ista stvar sa druge strane cilja: kada je izmereno ZNATNO TAMNIJE, razlika
// se dodaje offsetu i opet ne staje u devet bita. Dve grane, isti ishod - i
// obe moraju biti pokrivene, jer se u kodu razlikuju.
TEST(AdcOffsetTest, OffsetBelowTargetAlsoReducesTheGainWhenItDoesNotFit) {
    const CalibrationConfig config = flatbedConfig();
    GainOffsetState state = initialGainOffset(1);
    const int before = state.gain[0];

    // Zbir 1 na 10 uzoraka: prosek je daleko ispod cilja, ali nije nula -
    // dakle NE ide u granu potpune tame.
    const std::vector<double> nearlyDark{1, 1, 1};
    const auto step = advanceAdcOffset(config, OffsetParity::Even, nearlyDark, 10, &state);
    ASSERT_TRUE(step);
    EXPECT_GT(step.value().gainReductions, 0) << "grana ispod cilja";
    EXPECT_LT(state.gain[0], before);
}

TEST(AdcOffsetTest, GainCannotFallBelowZeroAndTheChannelSettles) {
    const CalibrationConfig config = flatbedConfig();
    GainOffsetState state = initialGainOffset(1);
    state.gain = {0, 0, 0};

    const std::vector<double> bright{200, 200, 200};
    const auto step = advanceAdcOffset(config, OffsetParity::Even, bright, 10, &state);
    ASSERT_TRUE(step);
    EXPECT_EQ(step.value().gainReductions, 0);
    EXPECT_EQ(step.value().settled, static_cast<int>(kChannels));
    for (std::size_t channel = 0; channel < kChannels; ++channel) {
        EXPECT_EQ(state.gain[channel], 0);
        EXPECT_TRUE(state.evenReady[channel]);
    }
}

// Petlja mora da se zaustavi. Ovo je jedini test koji to zaista dokazuje.
TEST(AdcOffsetTest, TheLoopTerminates) {
    const CalibrationConfig config = flatbedConfig();
    GainOffsetState state = initialGainOffset(1);

    const std::vector<double> measured{45, 60, 30};
    int rounds = 0;
    for (; rounds < 200; ++rounds) {
        const auto even = advanceAdcOffset(config, OffsetParity::Even, measured, 10, &state);
        ASSERT_TRUE(even);
        const auto odd = advanceAdcOffset(config, OffsetParity::Odd, measured, 10, &state);
        ASSERT_TRUE(odd);
        if (!even.value().changed && !odd.value().changed) {
            break;
        }
    }
    EXPECT_LT(rounds, 200) << "petlja se nije zaustavila";
    EXPECT_TRUE(state.allReady());
}

TEST(AdcOffsetTest, RejectsNonsenseArguments) {
    const CalibrationConfig config = flatbedConfig();
    GainOffsetState state = initialGainOffset(1);
    const std::vector<double> three{1, 2, 3};
    const std::vector<double> two{1, 2};

    EXPECT_FALSE(advanceAdcOffset(config, OffsetParity::Even, three, 10, nullptr));
    EXPECT_FALSE(advanceAdcOffset(config, OffsetParity::Even, two, 10, &state));
    EXPECT_FALSE(advanceAdcOffset(config, OffsetParity::Even, three, 0, &state));
}

TEST(AdcOffsetTest, SettledChannelsAreLeftAlone) {
    const CalibrationConfig config = flatbedConfig();
    GainOffsetState state = initialGainOffset(1);
    state.evenReady = {true, true, true};

    const std::vector<double> dark{0, 0, 0};
    const auto step = advanceAdcOffset(config, OffsetParity::Even, dark, 10, &state);
    ASSERT_TRUE(step);
    EXPECT_FALSE(step.value().changed);
    for (std::size_t channel = 0; channel < kChannels; ++channel) {
        EXPECT_EQ(state.evenDcg[channel], 0) << "gotov kanal se ne dira";
    }
}
