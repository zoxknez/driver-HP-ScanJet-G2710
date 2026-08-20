// Simulator uredjaja.
//
// Ovi testovi ne proveravaju engine nego SIMULATOR. Ako simulator laze -
// motor koji ne staje na kraju staze, lampa koja je odmah topla, senzor bez
// greske - onda sve faze koje se na njega oslanjaju testiraju nista.

#include "FailureInjector.h"
#include "G2710Profile.generated.h"
#include "SimTransport.h"
#include "TestTarget.h"
#include "VirtualCcd.h"
#include "VirtualLamp.h"
#include "VirtualMotor.h"
#include "rts8822/RegisterFile.h"
#include "rts8822/Registers.h"
#include "rts8822/Rts8822.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <vector>

using namespace g2710;
using namespace g2710::sim;

// --- motor -------------------------------------------------------------------

TEST(VirtualMotorTest, StartsAtHome) {
    VirtualMotor motor;
    EXPECT_EQ(motor.position(), 0);
    EXPECT_TRUE(motor.isAtHome());
    EXPECT_FALSE(motor.isAtFarLimit());
}

TEST(VirtualMotorTest, TravelMatchesFlatbedGeometry) {
    // 300 mm na 1200 dpi. Ako se ovo razidje sa profilom, testovi kretanja
    // mere pogresnu stazu.
    VirtualMotor motor;
    EXPECT_EQ(motor.geometry().travelMillimetres,
              profile::kConstraints.reflective.height);
    EXPECT_EQ(motor.geometry().resolution, profile::kMotor.resolution);
    EXPECT_NEAR(motor.geometry().maxPositionSteps(), 14173, 2);
}

TEST(VirtualMotorTest, StopsAtFarLimitAndCountsIt) {
    // Ovo je razlog zasto motor uopste postoji kao model. Kod koji zabija
    // glavu u kraj staze mora biti razlicit od ispravnog koda.
    VirtualMotor motor;
    motor.setDirection(MotorDirection::Forward);

    const MotorOutcome outcome = motor.step(100000);

    EXPECT_EQ(outcome, MotorOutcome::HitFarLimit);
    EXPECT_EQ(motor.position(), motor.geometry().maxPositionSteps());
    EXPECT_EQ(motor.farLimitHits(), 1);
}

TEST(VirtualMotorTest, StopsAtHomeGoingBackward) {
    VirtualMotor motor;
    motor.teleportTo(500);
    motor.setDirection(MotorDirection::Backward);

    EXPECT_EQ(motor.step(5000), MotorOutcome::HitHome);
    EXPECT_EQ(motor.position(), 0);
    EXPECT_TRUE(motor.isAtHome());
}

TEST(VirtualMotorTest, HomeSensorIsAWindowNotAPoint) {
    // Pravi home senzor prijavljuje "kod kuce" u opsegu, ne na jednoj tacki.
    VirtualMotor motor;
    motor.teleportTo(motor.geometry().homeWindowSteps);
    EXPECT_TRUE(motor.isAtHome());

    motor.teleportTo(motor.geometry().homeWindowSteps + 1);
    EXPECT_FALSE(motor.isAtHome());
}

TEST(VirtualMotorTest, StallStopsShortOfTheTarget) {
    VirtualMotor motor;
    motor.injectStall(30);

    EXPECT_EQ(motor.step(200), MotorOutcome::Stalled);
    EXPECT_EQ(motor.position(), 30) << "komanda prihvacena, glava se nije pomerila do kraja";
}

TEST(VirtualMotorTest, DisabledMotorDoesNotMove) {
    VirtualMotor motor;
    motor.setEnabled(false);
    EXPECT_EQ(motor.step(100), MotorOutcome::Disabled);
    EXPECT_EQ(motor.position(), 0);
}

TEST(VirtualMotorTest, TotalStepsAccumulateAcrossMoves) {
    VirtualMotor motor;
    motor.step(100);
    motor.setDirection(MotorDirection::Backward);
    motor.step(40);
    EXPECT_EQ(motor.totalSteps(), 140);
}

// --- lampa -------------------------------------------------------------------

TEST(VirtualLampTest, OffLampEmitsNothing) {
    VirtualLamp lamp;
    EXPECT_FALSE(lamp.isOn());
    EXPECT_EQ(lamp.level(), 0.0);
}

TEST(VirtualLampTest, ColdLampIsDimmerThanWarmOne) {
    // Bez ovoga Lamp_Warmup nema sta da meri i uvek bi prolazio iz prvog
    // pokusaja - merenje stabilnosti bi bilo mrtav kod.
    VirtualLamp lamp;
    lamp.turnOn();

    const double cold = lamp.level();
    lamp.advance(10000);
    const double warm = lamp.level();

    EXPECT_GT(warm, cold);
    EXPECT_NEAR(cold / warm, lamp.profile().coldFraction, 0.05);
}

TEST(VirtualLampTest, WarmupApproachesStableAsymptotically) {
    VirtualLamp lamp;
    lamp.turnOn();

    lamp.advance(1500);  // jedna vremenska konstanta
    const double afterOne = lamp.warmFraction();
    EXPECT_NEAR(afterOne, 1.0 - std::exp(-1.0), 0.01);

    lamp.advance(30000);
    EXPECT_GT(lamp.warmFraction(), 0.99);
}

TEST(VirtualLampTest, TurningOffResetsWarmup) {
    VirtualLamp lamp;
    lamp.turnOn();
    lamp.advance(10000);
    ASSERT_GT(lamp.warmFraction(), 0.9);

    lamp.turnOff();
    lamp.turnOn();
    EXPECT_EQ(lamp.onTimeMs(), 0u);
    EXPECT_LT(lamp.warmFraction(), 0.01);
}

TEST(VirtualLampTest, DutyCycleScalesTheLevel) {
    VirtualLamp full;
    full.turnOn();
    full.advance(30000);
    full.setDutyCycle(0x3F);

    VirtualLamp half;
    half.turnOn();
    half.advance(30000);
    half.setDutyCycle(0x1F);

    EXPECT_NEAR(half.level() / full.level(), 31.0 / 63.0, 0.02);
}

// --- test-meta ---------------------------------------------------------------

TEST(TestTargetTest, CalibrationStripsAreWhereTheyShouldBe) {
    const auto white = sampleTestTarget(100.0, 2.0);
    EXPECT_NEAR(white.red, target::kWhiteReflectance, 1e-9);

    const auto black = sampleTestTarget(100.0, 7.0);
    EXPECT_NEAR(black.red, target::kBlackReflectance, 1e-9);
}

TEST(TestTargetTest, ColorBarsSeparateChannels) {
    // Prva traka je crvena: R visoko, G i B nisko. Ako bi rekonstrukcija
    // zamenila kanale, ovo bi to pokazalo.
    const double barWidth = target::kWidthMm / target::kColorBarCount;
    const auto red = sampleTestTarget(barWidth * 0.5, 20.0);

    EXPECT_GT(red.red, 0.5);
    EXPECT_LT(red.green, 0.2);
    EXPECT_LT(red.blue, 0.2);
}

TEST(TestTargetTest, GradientIsLinearInReflectance) {
    // Meta je linearna, pa svaka nelinearnost u rezultatu dolazi od nas.
    const auto left = sampleTestTarget(0.0, 55.0);
    const auto middle = sampleTestTarget(target::kWidthMm / 2, 55.0);
    const auto right = sampleTestTarget(target::kWidthMm - 0.1, 55.0);

    EXPECT_NEAR(left.red, 0.0, 0.01);
    EXPECT_NEAR(middle.red, 0.5, 0.01);
    EXPECT_GT(right.red, 0.99);
}

TEST(TestTargetTest, OutsideTheSurfaceIsBlack) {
    EXPECT_EQ(sampleTestTarget(-1.0, 50.0).red, 0.0);
    EXPECT_EQ(sampleTestTarget(50.0, target::kHeightMm + 1.0).red, 0.0);
}

// --- CCD ---------------------------------------------------------------------

namespace {

VirtualLamp warmLamp() {
    VirtualLamp lamp;
    lamp.turnOn();
    lamp.advance(30000);
    return lamp;
}

double meanOf(const std::vector<std::uint16_t>& line) {
    double sum = 0.0;
    for (const auto value : line) {
        sum += value;
    }
    return line.empty() ? 0.0 : sum / static_cast<double>(line.size());
}

double spreadOf(const std::vector<std::uint16_t>& line) {
    if (line.empty()) {
        return 0.0;
    }
    const auto [lo, hi] = std::minmax_element(line.begin(), line.end());
    return static_cast<double>(*hi - *lo);
}

}  // namespace

TEST(VirtualCcdTest, ChannelsSampleDifferentRows) {
    // R, G i B redovi su fizicki razmaknuti - to je greska koju
    // LineOffsetCorrector iz G2710-6 mora da ispravi. Simulator koji bi sva
    // tri kanala citao sa istog reda ucinio bi taj kod neproverljivim.
    VirtualCcd ccd;

    EXPECT_EQ(ccd.channelRowOffset(CcdChannel::Red, 300), 0);
    EXPECT_GT(ccd.channelRowOffset(CcdChannel::Green, 300), 0);
    EXPECT_EQ(ccd.channelRowOffset(CcdChannel::Blue, 300),
              2 * ccd.channelRowOffset(CcdChannel::Green, 300));
}

TEST(VirtualCcdTest, RowOffsetMatchesProfileLineDistance) {
    // line_distance = 64 na 2400 dpi; motor je 1200, pa je pomak 32 koraka.
    VirtualCcd ccd;
    ASSERT_EQ(ccd.imperfections().lineDistanceAt2400, profile::kSensor.lineDistance);
    EXPECT_EQ(ccd.channelRowOffset(CcdChannel::Green, 300), 32);
}

TEST(VirtualCcdTest, WhiteStripIsNotFlat) {
    // NAJVAZNIJI TEST U OVOM FAJLU.
    //
    // Ako bi sirovo citanje bele trake bilo ravno, kalibracija iz G2710-5 ne
    // bi imala sta da poniStava i prazna implementacija bi prolazila. Greska
    // mora biti prisutna i dovoljno velika da se meri.
    VirtualCcd ccd;
    const VirtualLamp lamp = warmLamp();

    std::vector<std::uint16_t> line(ccd.pixelsPerLine(300));
    ASSERT_GT(line.size(), 100u);
    ccd.readLine(0, 300, CcdChannel::Red, lamp, line);

    const double mean = meanOf(line);
    const double spread = spreadOf(line);

    EXPECT_GT(mean, 1000.0) << "bela traka mora dati signal";
    EXPECT_GT(spread / mean, 0.05)
        << "sirovo citanje je preravno - kalibracija ne bi imala sta da ispravi";
}

TEST(VirtualCcdTest, EvenAndOddPixelsDifferSystematically) {
    // Parni i neparni pikseli idu kroz razdvojene ADC lance. Zato
    // st_gain_offset ima even/odd parove, a ne jednu vrednost.
    VirtualCcd ccd;
    const VirtualLamp lamp = warmLamp();

    std::vector<std::uint16_t> line(ccd.pixelsPerLine(300));
    ccd.readLine(0, 300, CcdChannel::Red, lamp, line);

    double evenSum = 0.0;
    double oddSum = 0.0;
    int evenCount = 0;
    int oddCount = 0;
    for (std::size_t i = 0; i < line.size(); ++i) {
        if ((i & 1u) == 0) { evenSum += line[i]; ++evenCount; }
        else               { oddSum += line[i];  ++oddCount; }
    }

    const double evenMean = evenSum / evenCount;
    const double oddMean = oddSum / oddCount;
    EXPECT_GT(std::abs(evenMean - oddMean), 100.0)
        << "parni i neparni se ne razlikuju - even/odd kalibracija bi bila mrtav kod";
}

TEST(VirtualCcdTest, DarkCurrentAppearsWithTheLampOff) {
    // Crni nivo nije nula. Black shading postoji upravo zbog toga.
    VirtualCcd ccd;
    VirtualLamp dark;  // ugasena

    std::vector<std::uint16_t> line(ccd.pixelsPerLine(300));
    ccd.readLine(0, 300, CcdChannel::Red, dark, line);

    EXPECT_GT(meanOf(line), 100.0) << "nema tamne struje";
    EXPECT_GT(spreadOf(line), 50.0) << "tamna struja je ista na svim pikselima";
}

TEST(VirtualCcdTest, IdealSensorIsFlatOnAUniformField) {
    // Kontrola: kada se greske iskljuce, isto citanje MORA biti ravno. Bez
    // ovoga test iznad ne bi razlikovao "ima greske" od "nesto je slomljeno".
    VirtualCcd ccd;
    ccd.makeIdeal();
    const VirtualLamp lamp = warmLamp();

    std::vector<std::uint16_t> line(ccd.pixelsPerLine(300));
    ccd.readLine(0, 300, CcdChannel::Red, lamp, line);

    EXPECT_LT(spreadOf(line) / meanOf(line), 0.01);
}

TEST(VirtualCcdTest, ReadingIsDeterministic) {
    VirtualCcd ccd;
    const VirtualLamp lamp = warmLamp();

    std::vector<std::uint16_t> first(ccd.pixelsPerLine(300));
    std::vector<std::uint16_t> second(ccd.pixelsPerLine(300));
    ccd.readLine(120, 300, CcdChannel::Green, lamp, first);
    ccd.readLine(120, 300, CcdChannel::Green, lamp, second);

    EXPECT_EQ(first, second) << "isti red daje razlicit rezultat - test ne bi bio ponovljiv";
}

// --- injekcija otkaza --------------------------------------------------------

TEST(FailureInjectorTest, FiresOnceThenStops) {
    FailureInjector injector;
    injector.injectOnce(TransferKind::ControlIn, ErrorCode::Timeout);

    EXPECT_EQ(injector.nextFault(TransferKind::ControlIn), ErrorCode::Timeout);
    EXPECT_FALSE(injector.nextFault(TransferKind::ControlIn).has_value());
    EXPECT_EQ(injector.firedCount(), 1);
}

TEST(FailureInjectorTest, DelayedFaultWaitsItsTurn) {
    FailureInjector injector;
    injector.schedule({TransferKind::BulkRead, ErrorCode::ShortTransfer, 2, 1});

    EXPECT_FALSE(injector.nextFault(TransferKind::BulkRead).has_value());
    EXPECT_FALSE(injector.nextFault(TransferKind::BulkRead).has_value());
    EXPECT_EQ(injector.nextFault(TransferKind::BulkRead), ErrorCode::ShortTransfer);
}

TEST(FailureInjectorTest, PermanentFaultNeverHeals) {
    // Iscupan kabl se sam od sebe ne popravlja.
    FailureInjector injector;
    injector.injectPermanent(TransferKind::Any, ErrorCode::TransportLost);

    for (int i = 0; i < 50; ++i) {
        EXPECT_EQ(injector.nextFault(TransferKind::ControlOut), ErrorCode::TransportLost);
    }
}

TEST(FailureInjectorTest, KindIsRespected) {
    FailureInjector injector;
    injector.injectOnce(TransferKind::BulkWrite, ErrorCode::Stalled);

    EXPECT_FALSE(injector.nextFault(TransferKind::ControlIn).has_value());
    EXPECT_EQ(injector.nextFault(TransferKind::BulkWrite), ErrorCode::Stalled);
}

// --- integracija kroz transport ----------------------------------------------

namespace {

class SimulatedDevice : public ::testing::Test {
protected:
    SimTransport device;
    rts8822::RegisterFile registers{device};
    rts8822::Rts8822 chip{device, SafetyGate{SafetyLevel::FullScan}};
};

}  // namespace

TEST_F(SimulatedDevice, HomeSensorRegisterFollowsTheMotor) {
    // Engine cita home preko registra, ne preko posebnog testnog kanala.
    auto atHome = chip.isHeadAtHome();
    ASSERT_TRUE(atHome.hasValue());
    EXPECT_TRUE(atHome.value());

    device.motor().teleportTo(5000);

    atHome = chip.isHeadAtHome();
    ASSERT_TRUE(atHome.hasValue());
    EXPECT_FALSE(atHome.value());
}

TEST_F(SimulatedDevice, LampStatusRegisterFollowsTheLamp) {
    auto status = chip.lampStatus();
    ASSERT_TRUE(status.hasValue());
    EXPECT_FALSE(status.value().flatbedOn);

    device.flatbedLamp().turnOn();

    status = chip.lampStatus();
    ASSERT_TRUE(status.hasValue());
    EXPECT_TRUE(status.value().flatbedOn);
    EXPECT_FALSE(status.value().tmaOn);
}

TEST_F(SimulatedDevice, TmaStatusReproducesTheReferenceDefect) {
    // Grana za RTS8822BL-03A trazi I bit u kLampStatus I selektor u kLampMode.
    // Ali selektor koji ona cita (Regs[0x154]) referenca nikada ne upisuje -
    // Lamp_Status_Set pise Regs[0x155]. Vidi D2 u docs/REFERENCE-DEFECTS.md.
    //
    // Simulator to REPRODUKUJE umesto da izgladi: upaljena TMA lampa se preko
    // Lamp_Status_Get NE vidi kao upaljena. Kada bi ovaj test poceo da pada,
    // znacilo bi da je neko "popravio" jednu stranu bez hardverske potvrde.
    device.tmaLamp().turnOn();

    auto status = chip.lampStatus();
    ASSERT_TRUE(status.hasValue());
    EXPECT_FALSE(status.value().tmaOn)
        << "TMA se vidi kao upaljena - defekt D2 je zaglagjen bez potvrde sa hardvera";

    // Statusni bit JESTE postavljen; samo selektor nedostaje.
    EXPECT_NE(device.peekRegister(rts8822::reg::kLampStatus) &
                  rts8822::reg::kLampStatusTmaBit,
              0);

    // Ako se selektor rucno postavi, grana proradi - cime je dokazano da je
    // problem bas u tome ko upisuje koji bajt, a ne u nasoj implementaciji.
    device.pokeRegister(rts8822::reg::kLampMode,
                        static_cast<std::uint8_t>(rts8822::reg::kLampModeTmaSelectBit));
    status = chip.lampStatus();
    ASSERT_TRUE(status.hasValue());
    EXPECT_TRUE(status.value().tmaOn);
}

TEST_F(SimulatedDevice, InjectedTransportLossSurfacesAsSuch) {
    device.faults().injectPermanent(TransferKind::Any, ErrorCode::TransportLost);

    const auto result = chip.isHeadAtHome();
    ASSERT_FALSE(result.hasValue());
    EXPECT_EQ(result.error().code, ErrorCode::TransportLost)
        << "gubitak veze mora stici do sloja iznad kao TransportLost";
}

TEST_F(SimulatedDevice, InjectedTimeoutIsDistinctFromTransportLoss) {
    // Razlika je sustinska: timeout ostavlja uredjaj upotrebljivim,
    // TransportLost invalidira poziciju glave. Vidi docs/SAFETY.md.
    device.faults().injectOnce(TransferKind::ControlIn, ErrorCode::Timeout);

    const auto first = chip.isHeadAtHome();
    ASSERT_FALSE(first.hasValue());
    EXPECT_EQ(first.error().code, ErrorCode::Timeout);

    EXPECT_TRUE(chip.isHeadAtHome().hasValue()) << "timeout se ne sme zalepiti";
}
