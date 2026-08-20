// Kontrola lampe i zagrevanje.
//
// Izvedeno iz rts8822.c:10738 Lamp_Status_Set, :2480 Lamp_PWM_Setup,
// :11138 Lamp_PWM_CheckStable.
//
// Dva testa na kraju drze zakljucane nesaglasnosti iz reference
// (docs/REFERENCE-DEFECTS.md). Oni NE tvrde da je nase ponasanje ispravno -
// tvrde sta referenca radi, da se razlika ne bi izgubila do H3.

#include "SimTransport.h"
#include "rts8822/Lamp.h"
#include "rts8822/RegisterFile.h"
#include "rts8822/Registers.h"
#include "transport/TraceRecorder.h"
#include "util/Clock.h"

#include <gtest/gtest.h>

#include <string>
#include <vector>

using namespace g2710;
using namespace g2710::rts8822;
using namespace std::chrono_literals;

namespace {

class LampTest : public ::testing::Test {
protected:
    sim::SimTransport device;
    TraceRecorder recorder{device};
    RegisterFile registers{recorder};
    ManualClock clock;

    Lamp lamp() { return Lamp{registers, SafetyGate{SafetyLevel::FullScan}, clock}; }
    std::string trace() const { return recorder.format(); }
};

}  // namespace

// --- ukljucivanje i iskljucivanje --------------------------------------------

TEST_F(LampTest, TurningOnFlatbedSetsItsBitOnly) {
    auto l = lamp();
    ASSERT_TRUE(l.setLamp(LampKind::Flatbed, true).hasValue());

    EXPECT_NE(device.peekRegister(reg::kLampStatus) & reg::kLampStatusFlbBit, 0);
    EXPECT_EQ(device.peekRegister(reg::kLampStatus) & reg::kLampStatusTmaBit, 0)
        << "TMA bit je upaljen zajedno sa flatbed lampom";
}

TEST_F(LampTest, TurningOffClearsTheBit) {
    auto l = lamp();
    ASSERT_TRUE(l.setLamp(LampKind::Flatbed, true).hasValue());
    ASSERT_TRUE(l.setLamp(LampKind::Flatbed, false).hasValue());

    EXPECT_EQ(device.peekRegister(reg::kLampStatus) & reg::kLampStatusFlbBit, 0);
}

TEST_F(LampTest, SwitchingToTmaClearsFlatbed) {
    // Oba bita postoje u istom registru; paljenje jedne lampe gasi drugu jer
    // se oba upisuju u istom prolazu.
    auto l = lamp();
    ASSERT_TRUE(l.setLamp(LampKind::Flatbed, true).hasValue());
    ASSERT_TRUE(l.setLamp(LampKind::Tma, true).hasValue());

    EXPECT_EQ(device.peekRegister(reg::kLampStatus) & reg::kLampStatusFlbBit, 0);
    EXPECT_NE(device.peekRegister(reg::kLampStatus) & reg::kLampStatusTmaBit, 0);
}

TEST_F(LampTest, PreservesUnrelatedBitsInTheStatusRegister) {
    device.pokeRegister(reg::kLampStatus, 0x9F);  // sve van maski 0x60

    auto l = lamp();
    ASSERT_TRUE(l.setLamp(LampKind::Flatbed, true).hasValue());

    EXPECT_EQ(device.peekRegister(reg::kLampStatus) & 0x9F, 0x9F)
        << "read-modify-write je pregazio bitove van maski";
}

TEST_F(LampTest, SettleDelayHappensBetweenTheTwoWrites) {
    // rts8822.c:10793 - referenca spava 200 ms izmedju upisa u status i upisa
    // u registar izbora. Preskakanje bi menjalo izbor pre nego sto se lampa
    // stvarno prebacila.
    auto l = lamp();
    const Instant before = clock.now();
    ASSERT_TRUE(l.setLamp(LampKind::Flatbed, true).hasValue());

    EXPECT_GE(clock.now() - before, kLampSwitchSettle);
}

TEST_F(LampTest, RequiresLampLevel) {
    Lamp restricted{registers, SafetyGate{SafetyLevel::ReadOnly}, clock};
    const Status status = restricted.setLamp(LampKind::Flatbed, true);

    ASSERT_FALSE(status.hasValue());
    EXPECT_EQ(status.error().code, ErrorCode::SafetyViolation);
    EXPECT_TRUE(trace().empty()) << "odbijena operacija je ipak nesto poslala";
}

// --- PWM ---------------------------------------------------------------------

TEST_F(LampTest, PwmSetupSkipsTheWriteWhenValueAlreadyMatches) {
    // Referenca upisuje samo ako se razlikuje. Warmup ovo zove cesto, pa je
    // usteda stvarna.
    auto l = lamp();
    ASSERT_TRUE(l.setupPwm(LampKind::Flatbed).hasValue());

    recorder.clear();
    ASSERT_TRUE(l.setupPwm(LampKind::Flatbed).hasValue());

    EXPECT_EQ(trace(), "CTL DI: c0 04 e948 0100 0002\n")
        << "drugi poziv je ipak upisao";
}

TEST_F(LampTest, PwmSetupWritesWhenValueDiffers) {
    device.pokeRegister(reg::kLampPwm, 0x2A);

    auto l = lamp();
    ASSERT_TRUE(l.setupPwm(LampKind::Flatbed).hasValue());

    EXPECT_NE(trace().find("CTL DO: 40 04 e948 0000 0002\n"), std::string::npos);
}

// --- merenje stabilnosti -----------------------------------------------------

TEST_F(LampTest, WarmupMeasuresInsteadOfWaitingAFixedTime) {
    // Nivo raste pa se smiri; merenje mora stati cim se dva ocitavanja
    // priblize, a ne posle punih 10 sekundi.
    std::vector<double> levels{1000, 4000, 6000, 6050, 6050, 6050};
    std::size_t index = 0;

    auto l = lamp();
    const auto result = l.waitUntilStable([&]() -> Result<double> {
        const double value = levels[std::min(index, levels.size() - 1)];
        ++index;
        return value;
    });

    ASSERT_TRUE(result.hasValue());
    EXPECT_TRUE(result.value().stabilised);
    EXPECT_LT(result.value().samples, static_cast<int>(levels.size()))
        << "merenje je proslo kroz sva ocitavanja umesto da stane ranije";
}

TEST_F(LampTest, WarmupGivesUpWhenTheLevelNeverSettles) {
    // Lampa koja stalno luta ne sme blokirati zauvek - postoji ukupan rok.
    double level = 0.0;

    auto l = lamp();
    const auto result = l.waitUntilStable([&]() -> Result<double> {
        level += 5000.0;  // uvek dovoljno veliki skok
        return level;
    });

    ASSERT_TRUE(result.hasValue());
    EXPECT_FALSE(result.value().stabilised);
    EXPECT_GT(result.value().samples, 1);
}

TEST_F(LampTest, ThresholdIsScaledByOneHundredth) {
    // rts8822.c:11157 - diff * 0.01. Sa diff = 100 prag je 1.0, ne 100.
    StabilityCriterion criterion;
    criterion.diff = 100.0;

    // Razlika od 5 je VECA od praga 1.0, pa merenje ne sme stati.
    //
    // Vrednosti NE smeju poceti od nule: referenca krece sa last_colour = 0,
    // pa bi prvo ocitavanje od 0 odmah zadovoljilo prag i test bi prosao iz
    // pogresnog razloga.
    std::vector<double> levels{5000, 5005, 5010, 5015, 5020, 5020};
    std::size_t index = 0;

    auto l = lamp();
    const auto result = l.waitUntilStable(
        [&]() -> Result<double> {
            const double value = levels[std::min(index, levels.size() - 1)];
            ++index;
            return value;
        },
        criterion);

    ASSERT_TRUE(result.hasValue());
    EXPECT_GT(result.value().samples, 2)
        << "prag nije skaliran - stalo je na razlici koja je iznad njega";
}

TEST_F(LampTest, ReadFailureIsPropagatedNotSwallowed) {
    auto l = lamp();
    const auto result = l.waitUntilStable(
        []() -> Result<double> { return fail(ErrorCode::TransportLost, "read"); });

    ASSERT_FALSE(result.hasValue());
    EXPECT_EQ(result.error().code, ErrorCode::TransportLost);
}

TEST_F(LampTest, FullWarmupWaitsOverdriveBeforeMeasuring) {
    // overdrive_flb je 10 s slepog cekanja PRE nego sto merenje uopste pocne.
    auto l = lamp();
    const Instant before = clock.now();

    double level = 6000.0;
    const auto result = l.warmUp(LampKind::Flatbed,
                                 [&]() -> Result<double> { return level; });

    ASSERT_TRUE(result.hasValue());
    EXPECT_GE(clock.now() - before, kOverdriveFlatbed);
}

// --- zakljucane nesaglasnosti iz reference -----------------------------------

TEST_F(LampTest, TmaSelectBitIsWrittenAndReadFromDifferentBytes) {
    // D2 iz docs/REFERENCE-DEFECTS.md.
    //
    // Lamp_Status_Set upisuje bit izbora u Regs[0x155]; Lamp_Status_Get ga u
    // BL-03A grani cita iz Regs[0x154]. Ovaj test NE tvrdi da je to ispravno -
    // tvrdi da je tako, da se razlika ne bi izgubila pre nego sto je H3 razresi.
    auto l = lamp();
    ASSERT_TRUE(l.setLamp(LampKind::Tma, true).hasValue());

    EXPECT_NE(device.peekRegister(reg::kLampSelect) & reg::kLampSelectTmaBit, 0)
        << "Set nije upisao bit izbora u Regs[0x155]";

    // Bajt koji Get zapravo testira ostaje nedirnut.
    EXPECT_EQ(device.peekRegister(reg::kLampMode) &
                  static_cast<std::uint8_t>(reg::kLampModeTmaSelectBit),
              0)
        << "Regs[0x154] je promenjen - nesaglasnost D2 je nestala, "
           "sto znaci da je neko 'popravio' jednu stranu bez hardverske potvrde";
}

TEST_F(LampTest, TurnOnArgumentIsHonouredUnlikeTheReferenceCall) {
    // D1 iz docs/REFERENCE-DEFECTS.md.
    //
    // Lamp_Warmup poziva Lamp_Status_Set sa FALSE da bi UPALIO TMA lampu. To
    // radi samo zahvaljujuci triku (lamp-1)|turn_on u default grani, koje u
    // BL-03A grani nema. Mi `on` postujemo doslovno.
    auto l = lamp();

    ASSERT_TRUE(l.setLamp(LampKind::Tma, false).hasValue());
    EXPECT_EQ(device.peekRegister(reg::kLampStatus) & reg::kLampStatusTmaBit, 0)
        << "setLamp(Tma, false) je upalio lampu - reprodukovan je defekt D1";

    ASSERT_TRUE(l.setLamp(LampKind::Tma, true).hasValue());
    EXPECT_NE(device.peekRegister(reg::kLampStatus) & reg::kLampStatusTmaBit, 0);
}
