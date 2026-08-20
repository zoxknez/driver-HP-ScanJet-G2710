// CCD timing profili primenjeni na registarski bank.
//
// Izvedeno iz rts8822.c:5066 RTS_Setup_SensorTiming i :5028
// Timing_SetLinearImageSensorClock.
//
// Ovo su ciste transformacije bank-a, pa im ne treba ni transport ni
// simulator - sto ih cini najjeftinijim i najstrozim testovima u projektu.

#include "G2710Profile.generated.h"
#include "rts8822/RegisterBank.h"
#include "rts8822/SensorTiming.h"

#include <gtest/gtest.h>

#include <cstddef>

using namespace g2710::rts8822;
namespace profile = g2710::profile;

namespace {

template <typename T, std::size_t N>
constexpr std::size_t countOf(const T (&)[N]) {
    return N;
}

}  // namespace

// --- 36-bitna aritmetika u double-u -----------------------------------------

TEST(DoubleBits, LowByteHandlesValuesBeyond32Bits) {
    // 8589934588 = 0x1FFFFFFFC. Prelazi 32 bita, pa je zato u referenci double.
    constexpr double kPhase = 8589934588.0;

    EXPECT_EQ(lowByteOfDouble(kPhase), 0xFC);
    EXPECT_EQ(lowByteOfDouble(shiftRightDouble(kPhase, 0x08)), 0xFF);
    EXPECT_EQ(lowByteOfDouble(shiftRightDouble(kPhase, 0x10)), 0xFF);
    EXPECT_EQ(lowByteOfDouble(shiftRightDouble(kPhase, 0x18)), 0xFF);
    EXPECT_EQ(lowByteOfDouble(shiftRightDouble(kPhase, 0x20)) & 0x0F, 0x01);
}

TEST(DoubleBits, AllOnes36BitMaskSpreadsCorrectly) {
    // 68719476735 = 0xFFFFFFFFF, sva 36 bita postavljena. Javlja se u vise
    // timing profila kao "faza iskljucena".
    constexpr double kAllOnes = 68719476735.0;

    EXPECT_EQ(lowByteOfDouble(kAllOnes), 0xFF);
    EXPECT_EQ(lowByteOfDouble(shiftRightDouble(kAllOnes, 0x20)) & 0x0F, 0x0F);
}

TEST(DoubleBits, ShiftAboveLimitIsZero) {
    // get_shrd vraca 0 iznad 0x40 umesto da nastavi da deli.
    EXPECT_EQ(shiftRightDouble(68719476735.0, 0x41), 0.0);
}

TEST(DoubleBits, ValuesUnder32BitsAreUntouched) {
    EXPECT_EQ(lowByteOfDouble(0.0), 0x00);
    EXPECT_EQ(lowByteOfDouble(255.0), 0xFF);
    EXPECT_EQ(lowByteOfDouble(256.0), 0x00);
    EXPECT_EQ(lowByteOfDouble(shiftRightDouble(256.0, 8)), 0x01);
}

// --- rasprsivanje po bank-u --------------------------------------------------

TEST(RegisterBankSpread, LsbAndMsbAreMirrorImages) {
    RegisterBank lsb;
    RegisterBank msb;

    lsb.setLsb(0x100, 0x00123456, 3);
    msb.setMsb(0x100, 0x00123456, 3);

    EXPECT_EQ(lsb.at(0x100), 0x56);
    EXPECT_EQ(lsb.at(0x101), 0x34);
    EXPECT_EQ(lsb.at(0x102), 0x12);

    EXPECT_EQ(msb.at(0x100), 0x12);
    EXPECT_EQ(msb.at(0x101), 0x34);
    EXPECT_EQ(msb.at(0x102), 0x56);
}

TEST(RegisterBankSpread, AddressAndIndexRefersToTheSameByte) {
    // Referenca naizmenicno koristi Read_Byte(0xE96F) i Regs[0x16F].
    RegisterBank bank;
    bank.setAddress(0xE96F, 0xAB);
    EXPECT_EQ(bank.at(0x16F), 0xAB);
    EXPECT_EQ(RegisterBank::indexOf(0xE96F), 0x16Fu);
}

TEST(RegisterBankSpread, OutOfRangeSizeIsIgnoredNotClamped) {
    // data_lsb_set odbija size van 1..4 i ne dira nista.
    RegisterBank bank;
    bank.set(0x200, 0x5A);
    bank.setLsb(0x200, 0xFFFFFFFF, 0);
    bank.setLsb(0x200, 0xFFFFFFFF, 5);
    EXPECT_EQ(bank.at(0x200), 0x5A);
}

// --- timing profil 0 (2400 dpi) ---------------------------------------------

TEST(SensorTiming, FirstClockPhaseMatchesHandComputedBytes) {
    // kTimings[0].cph[0].p1 = 8589934588 = 0x1FFFFFFFC, ps=0, ge=1, go=1.
    // Ide na 0x48..0x4C.
    RegisterBank bank;
    ASSERT_TRUE(applySensorTiming(bank, 0));

    EXPECT_EQ(bank.at(0x48), 0xFC);
    EXPECT_EQ(bank.at(0x49), 0xFF);
    EXPECT_EQ(bank.at(0x4A), 0xFF);
    EXPECT_EQ(bank.at(0x4B), 0xFF);

    // Peti bajt: nibble 0x01, ps u bitu 6, ge u bitu 5, go u bitu 4.
    const std::uint8_t fifth = bank.at(0x4C);
    EXPECT_EQ(fifth & 0x0F, 0x01) << "gornja cetiri bita 36-bitne maske";
    EXPECT_EQ((fifth >> 6) & 1, 0) << "ps";
    EXPECT_EQ((fifth >> 5) & 1, 1) << "ge";
    EXPECT_EQ((fifth >> 4) & 1, 1) << "go";
}

TEST(SensorTiming, SecondPhaseOfFirstClockIsZero) {
    // kTimings[0].cph[0].p2 = 0, ide na 0x4D..0x51.
    RegisterBank bank;
    ASSERT_TRUE(applySensorTiming(bank, 0));

    for (std::size_t i = 0x4D; i <= 0x50; ++i) {
        EXPECT_EQ(bank.at(i), 0x00) << "offset " << i;
    }
    EXPECT_EQ(bank.at(0x51) & 0x0F, 0x00);
}

TEST(SensorTiming, TransferGatesShareOneByteWithPulseWidth) {
    // cvtrp[0..2] su bitovi 7/6/5, a cvtrfpw donjih pet bita ISTOG bajta.
    // Upis kao jedna vrednost bi obrisao gate-ove.
    RegisterBank bank;
    ASSERT_TRUE(applySensorTiming(bank, 0));

    const profile::Timing& t = profile::kTimings[0];
    const std::uint8_t gates = bank.at(timing_offset::kTransferGates);

    EXPECT_EQ((gates >> 7) & 1, t.cvtrp[0]);
    EXPECT_EQ((gates >> 6) & 1, t.cvtrp[1]);
    EXPECT_EQ((gates >> 5) & 1, t.cvtrp[2]);
    EXPECT_EQ(gates & 0x1F, t.cvtrfpw);
}

TEST(SensorTiming, NegativeClampEndSpreadsAsAllOnes) {
    // kTimings[0].clampe = -1. data_lsb_set radi nad SANE_Int, pa se -1
    // rasprsi kao 0xFF 0xFF 0xFF - to je stvarna vrednost, ne greska.
    ASSERT_EQ(profile::kTimings[0].clampe, -1);

    RegisterBank bank;
    ASSERT_TRUE(applySensorTiming(bank, 0));

    EXPECT_EQ(bank.at(timing_offset::kClampEnd + 0), 0xFF);
    EXPECT_EQ(bank.at(timing_offset::kClampEnd + 1), 0xFF);
    EXPECT_EQ(bank.at(timing_offset::kClampEnd + 2), 0xFF);
}

TEST(SensorTiming, CorrelatedDoubleSampleValuesLandInSixBitFields) {
    RegisterBank bank;
    ASSERT_TRUE(applySensorTiming(bank, 0));

    const profile::Timing& t = profile::kTimings[0];
    EXPECT_EQ(bank.at(timing_offset::kCdss0) & 0x3F, t.cdss[0]);
    EXPECT_EQ(bank.at(timing_offset::kCdsc0) & 0x3F, t.cdsc[0]);
    EXPECT_EQ(bank.at(timing_offset::kCdss1) & 0x3F, t.cdss[1]);
    EXPECT_EQ(bank.at(timing_offset::kCdsc1) & 0x3F, t.cdsc[1]);
    EXPECT_EQ(bank.at(timing_offset::kCnpp) & 0x3F, t.cnpp);
}

TEST(SensorTiming, AdcClockTwoCarriesItsFlagInBitFour) {
    RegisterBank bank;
    ASSERT_TRUE(applySensorTiming(bank, 0));

    const std::uint8_t fifth = bank.at(timing_offset::kAdcClock1 + 4);
    EXPECT_EQ((fifth >> 4) & 1, profile::kTimings[0].adcclkp2e & 1);
}

TEST(SensorTiming, PreservesBitsItDoesNotOwn) {
    // Bit 7 petog bajta clock faze mora prezivet (Regs[0x04] &= 0x80).
    RegisterBank bank;
    bank.set(0x4C, 0xFF);

    ASSERT_TRUE(applySensorTiming(bank, 0));
    EXPECT_EQ(bank.at(0x4C) & 0x80, 0x80) << "bit 7 nije sacuvan";
}

TEST(SensorTiming, DoesNotTouchBytesOutsideItsRange) {
    RegisterBank bank;
    // Registri koje timing ne dira: kontrola i lampa.
    bank.set(0x000, 0xA5);
    bank.set(0x146, 0x5A);
    bank.set(0x16F, 0x3C);

    ASSERT_TRUE(applySensorTiming(bank, 0));

    EXPECT_EQ(bank.at(0x000), 0xA5);
    EXPECT_EQ(bank.at(0x146), 0x5A);
    EXPECT_EQ(bank.at(0x16F), 0x3C);
}

// --- svi profili -------------------------------------------------------------

TEST(SensorTiming, EveryExtractedProfileApplies) {
    for (std::size_t i = 0; i < countOf(profile::kTimings); ++i) {
        RegisterBank bank;
        EXPECT_TRUE(applySensorTiming(bank, static_cast<int>(i)))
            << "profil " << i << " nije primenjen";
    }
}

TEST(SensorTiming, OutOfRangeIndexIsRefused) {
    RegisterBank bank;
    EXPECT_FALSE(applySensorTiming(bank, -1));
    EXPECT_FALSE(applySensorTiming(bank, static_cast<int>(countOf(profile::kTimings))));
}

TEST(SensorTiming, EveryScanModeTimingIndexProducesADistinctBank) {
    // Scan mode tabela referise timing profile po indeksu. Ako bi dva razlicita
    // profila davala isti bank, jedan od njih ne bi imao svrhu - a to bi
    // znacilo da je ekstrakcija nesto slepila.
    int identicalPairs = 0;
    for (std::size_t a = 0; a < countOf(profile::kTimings); ++a) {
        RegisterBank first;
        ASSERT_TRUE(applySensorTiming(first, static_cast<int>(a)));

        for (std::size_t b = a + 1; b < countOf(profile::kTimings); ++b) {
            RegisterBank second;
            ASSERT_TRUE(applySensorTiming(second, static_cast<int>(b)));

            bool same = true;
            for (std::size_t i = 0; i < RegisterBank::size() && same; ++i) {
                same = first.at(i) == second.at(i);
            }
            if (same) {
                ++identicalPairs;
            }
        }
    }
    // Neki profili se legitimno ponavljaju (ista rezolucija, drugi rezim), pa
    // ne trazimo nulu - trazimo da nisu SVI isti.
    const auto total = countOf(profile::kTimings);
    const std::size_t allPairs = total * (total - 1) / 2;
    EXPECT_LT(static_cast<std::size_t>(identicalPairs), allPairs)
        << "svi timing profili daju isti bank - ekstrakcija je slepila tabelu";
}
