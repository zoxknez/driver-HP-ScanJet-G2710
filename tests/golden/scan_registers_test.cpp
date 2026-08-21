// Geometrija i format skeniranja u registrima.
//
// Izvedeno iz RTS_Setup_Coords (rts8822.c:9229), RTS_Setup_Depth (:8768),
// RTS_Setup_Line_Distances (:8678), RTS_Execute (:3947), RTS_Warm_Reset
// (:3913) i RTS_WaitScanEnd (:3868).
//
// Grupa na kraju drzi zakljucanim D3: sestobitno polje se odseca TIHO, tacno
// kao u referenci. To se ovde ne popravlja - popravlja se u planeru, koji
// hardversko poravnanje uopste ne trazi kada ne staje.

#include "SimTransport.h"
#include "rts8822/RegisterFile.h"
#include "rts8822/Registers.h"
#include "rts8822/ScanRegisters.h"
#include "transport/TraceRecorder.h"

#include <gtest/gtest.h>

#include <chrono>
#include <string>

using namespace g2710;
using namespace g2710::rts8822;

namespace {

class ScanRegistersTest : public ::testing::Test {
protected:
    sim::SimTransport device;
    TraceRecorder recorder{device};
    RegisterFile registers{recorder};

    ScanRegisters scan(SafetyLevel level = SafetyLevel::FullScan) {
        return ScanRegisters{registers, SafetyGate{level}};
    }
    std::string trace() const { return recorder.format(); }

    // Najmanji prolaz koji simulator ume da protumaci: odnos rezolucija,
    // dummy redovi, geometrija, format, pa pokretanje.
    Status startSmallPass(ScanRegisters& s) {
        if (const Status r = s.setResolutionRatio(8); !r) {
            return r;
        }
        if (const Status r = s.setDummyLine(1); !r) {
            return r;
        }
        CoordinateScaling scaling;
        scaling.resolutionRatio = 8;
        scaling.dummyLine = 1;
        if (const Status r = s.setGeometry(toRegisterCoordinates({1, 1, 32, 8}, scaling)); !r) {
            return r;
        }
        if (const Status r = s.setFormat({3, image::DepthCode::Bits8, {}}); !r) {
            return r;
        }
        return s.execute();
    }
};

}  // namespace

// --- racun koordinata (bez uredjaja) -----------------------------------------

TEST(ScanCoordinates, HorizontalIsExpressedInSensorUnits) {
    CoordinateScaling scaling;
    scaling.resolutionRatio = 8;  // 2400 / 300
    scaling.dummyLine = 1;

    const ScanGeometry pixels{100, 50, 600, 400};
    const ScanGeometry out = toRegisterCoordinates(pixels, scaling);

    EXPECT_EQ(out.width, 600 * 8);
    EXPECT_EQ(out.left, 801) << "800 je parno, pa se podize na neparno";
}

// rts8822.c:9159. Referenca ovo ne obrazlaze; tice se parnih i neparnih
// piksela senzora, gde pomeraj za jedan menja koji lanac cita koji piksel.
TEST(ScanCoordinates, LeftEdgeIsAlwaysOdd) {
    CoordinateScaling scaling;
    scaling.resolutionRatio = 4;
    scaling.dummyLine = 1;

    for (int left = 0; left < 20; ++left) {
        const ScanGeometry out = toRegisterCoordinates({left, 1, 100, 100}, scaling);
        EXPECT_EQ(out.left % 2, 1) << "levo = " << left;
    }
}

// rts8822.c:9145 - nula postaje jedan PRE mnozenja.
TEST(ScanCoordinates, ZeroOriginIsRaisedToOne) {
    CoordinateScaling scaling;
    scaling.resolutionRatio = 8;
    scaling.dummyLine = 2;

    const ScanGeometry out = toRegisterCoordinates({0, 0, 100, 100}, scaling);
    EXPECT_EQ(out.left, 9) << "1 * 8 = 8, pa na neparno";
    EXPECT_EQ(out.top, 2) << "1 * 2";
}

// Visina u registrima je VECA od trazene: cip mora preskenirati onoliko
// redova koliko poravnanje kanala pojede pre prvog ispravnog reda.
TEST(ScanCoordinates, HeightIsPaddedByWhateverAlignmentConsumes) {
    CoordinateScaling scaling;
    scaling.resolutionRatio = 8;
    scaling.dummyLine = 1;
    scaling.lineOffsetPadding = 16;
    scaling.softwareLineDistance = 0;

    const ScanGeometry out = toRegisterCoordinates({1, 1, 100, 400}, scaling);
    EXPECT_EQ(out.height, 400 + 16);

    scaling.lineOffsetPadding = 0;
    scaling.softwareLineDistance = 9;
    EXPECT_EQ(toRegisterCoordinates({1, 1, 100, 400}, scaling).height, 400 + 9);
}

TEST(ScanCoordinates, InverseRecoversWidthAndTheScannedLineCount) {
    CoordinateScaling scaling;
    scaling.resolutionRatio = 8;
    scaling.dummyLine = 2;
    scaling.lineOffsetPadding = 16;

    const ScanGeometry pixels{100, 50, 600, 400};
    const ScanGeometry back = fromRegisterCoordinates(toRegisterCoordinates(pixels, scaling),
                                                      scaling);

    EXPECT_EQ(back.width, 600);
    EXPECT_EQ(back.top, 50);
    EXPECT_EQ(back.height, 400 + 16)
        << "povratak daje redove koje cip STVARNO skenira, ne trazene";
}

TEST(ScanCoordinates, InvalidScalingProducesNothingRatherThanGarbage) {
    CoordinateScaling scaling;
    scaling.resolutionRatio = 0;
    EXPECT_EQ(toRegisterCoordinates({1, 1, 100, 100}, scaling).width, 0);
    EXPECT_EQ(fromRegisterCoordinates({1, 1, 100, 100}, scaling).width, 0);
}

// --- duzina reda -------------------------------------------------------------

TEST(ScanBytesPerLine, FollowsChannelsAndDepth) {
    const ScanGeometry geometry{0, 0, 100, 1};

    EXPECT_EQ(bytesPerLine(geometry, {3, image::DepthCode::Bits8, {}}), 300u);
    EXPECT_EQ(bytesPerLine(geometry, {3, image::DepthCode::Bits16, {}}), 600u);
    EXPECT_EQ(bytesPerLine(geometry, {1, image::DepthCode::Bits8, {}}), 100u);
    EXPECT_EQ(bytesPerLine(geometry, {1, image::DepthCode::Lineart, {}}), 13u);
    EXPECT_EQ(bytesPerLine({0, 0, 0, 1}, {3, image::DepthCode::Bits8, {}}), 0u);
}

// --- geometrija kroz uredjaj -------------------------------------------------

TEST_F(ScanRegistersTest, GeometryRoundTrips) {
    auto s = scan();
    const ScanGeometry written{801, 100, 4800, 3200};
    ASSERT_TRUE(s.setGeometry(written));

    const auto read = s.geometry();
    ASSERT_TRUE(read);
    EXPECT_EQ(read.value().left, written.left);
    EXPECT_EQ(read.value().top, written.top);
    EXPECT_EQ(read.value().width, written.width);
    EXPECT_EQ(read.value().height, written.height);
}

// Uspravne koordinate su 20-bitne: 16 u paru, gornja 4 u niblu zajednickog
// bajta. Bez niblova bi visoka rezolucija tiho izgubila donji deo slike.
TEST_F(ScanRegistersTest, VerticalCoordinatesCarryTwentyBits) {
    auto s = scan();
    const ScanGeometry written{1, 70000, 1000, 50000};
    ASSERT_TRUE(s.setGeometry(written));

    const auto read = s.geometry();
    ASSERT_TRUE(read);
    EXPECT_EQ(read.value().top, 70000);
    EXPECT_EQ(read.value().height, 50000);

    // Oba nibla stoje u istom bajtu i ne smeju se gaziti.
    const std::uint8_t high = device.peekRegister(reg::kScanVerticalHigh);
    EXPECT_EQ(high & reg::kScanTopHighMask, (70000 >> 16) & 0x0F);
    EXPECT_EQ((high & reg::kScanBottomHighMask) >> 4, (120000 >> 16) & 0x0F);
}

TEST_F(ScanRegistersTest, GeometryRejectsWhatDoesNotFit) {
    auto s = scan();
    EXPECT_FALSE(s.setGeometry({0, 0, 0, 100})) << "prazna sirina";
    EXPECT_FALSE(s.setGeometry({0, 0, 100, 0})) << "prazna visina";
    EXPECT_FALSE(s.setGeometry({-1, 0, 100, 100}));
    EXPECT_FALSE(s.setGeometry({60000, 0, 10000, 100})) << "desno preko 16 bita";
    EXPECT_FALSE(s.setGeometry({0, 1100000, 100, 100})) << "dole preko 20 bita";
}

TEST_F(ScanRegistersTest, GeometryNeedsAcquireLevel) {
    auto s = scan(SafetyLevel::Motor);
    const Status refused = s.setGeometry({1, 1, 100, 100});
    ASSERT_FALSE(refused);
    EXPECT_EQ(refused.error().code, ErrorCode::SafetyViolation);

    // Citanje je i dalje nivo 1.
    EXPECT_TRUE(scan(SafetyLevel::ReadOnly).geometry());
}

// --- format ------------------------------------------------------------------

TEST_F(ScanRegistersTest, FormatRoundTrips) {
    auto s = scan();
    ScanFormat written;
    written.channelsPerDot = 3;
    written.depthCode = image::DepthCode::Bits16;
    written.threshold = {700, 690};
    ASSERT_TRUE(s.setFormat(written));

    const auto read = s.format();
    ASSERT_TRUE(read);
    EXPECT_EQ(read.value().channelsPerDot, 3);
    EXPECT_EQ(read.value().depthCode, image::DepthCode::Bits16);
    EXPECT_EQ(read.value().threshold.high, 700);
    EXPECT_EQ(read.value().threshold.low, 690);
}

TEST_F(ScanRegistersTest, DepthCodeLandsInTheShadingRegister) {
    auto s = scan();
    device.pokeRegister(reg::kDepthCode, 0xCF);  // sve osim polja dubine

    ScanFormat format;
    format.channelsPerDot = 1;
    format.depthCode = image::DepthCode::Lineart;
    ASSERT_TRUE(s.setFormat(format));

    const std::uint8_t value = device.peekRegister(reg::kDepthCode);
    EXPECT_EQ((value & reg::kDepthCodeMask) >> 4, 3) << "lineart je 3";
    EXPECT_EQ(value & ~reg::kDepthCodeMask, 0xCF & ~reg::kDepthCodeMask)
        << "ostali bitovi su pregazeni";
}

TEST_F(ScanRegistersTest, FormatRejectsImpossibleChannelCounts) {
    auto s = scan();
    EXPECT_FALSE(s.setFormat({0, image::DepthCode::Bits8, {}}));
    EXPECT_FALSE(s.setFormat({4, image::DepthCode::Bits8, {}}));
}

// --- pomaci redova, i D3 -----------------------------------------------------

TEST_F(ScanRegistersTest, LineOffsetsRoundTripWhenTheyFit) {
    auto s = scan();
    const image::LineOffsetRegisters written{2, 16, 18, 32, 34};
    ASSERT_TRUE(s.setLineOffsets(written));

    const auto read = s.lineOffsets();
    ASSERT_TRUE(read);
    EXPECT_EQ(read.value().evenOdd, 2);
    EXPECT_EQ(read.value().lineDistance, 16);
    EXPECT_EQ(read.value().doubleLineDistance, 32);
    EXPECT_EQ(read.value().doublePlusEvenOdd, 34);
}

// Ovde se D3 vidi u kodu: 64 ulazi, 0 izlazi, i nista se ne buni.
TEST_F(ScanRegistersTest, ValuesAboveSixBitsAreTruncatedSilently) {
    auto s = scan();
    image::LineOffsetRegisters at1200;
    at1200.lineDistance = 32;
    at1200.doubleLineDistance = 64;  // ne staje

    ASSERT_TRUE(s.setLineOffsets(at1200)) << "referenca ne prijavljuje gubitak";

    const auto read = s.lineOffsets();
    ASSERT_TRUE(read);
    EXPECT_EQ(read.value().lineDistance, 32) << "zeleni jos staje";
    EXPECT_EQ(read.value().doubleLineDistance, 0)
        << "plavi je odsecen - kanal ostaje nepomeren";
}

TEST_F(ScanRegistersTest, ClearingOffsetsTurnsHardwareAlignmentOff) {
    auto s = scan();
    ASSERT_TRUE(s.setLineOffsets({2, 16, 18, 32, 34}));
    ASSERT_TRUE(s.clearLineOffsets());

    const auto read = s.lineOffsets();
    ASSERT_TRUE(read);
    EXPECT_EQ(read.value().largest(), 0);
}

TEST_F(ScanRegistersTest, OffsetFieldsDoNotDisturbTheirNeighbours) {
    auto s = scan();
    device.pokeRegister(reg::kLineOffsetDouble, 0xC0);  // gornja dva bita zauzeta
    ASSERT_TRUE(s.setLineOffsets({0, 0, 0, 5, 0}));

    const std::uint8_t value = device.peekRegister(reg::kLineOffsetDouble);
    EXPECT_EQ(value & reg::kLineOffsetMask, 5);
    EXPECT_EQ(value & 0xC0, 0xC0) << "bitovi izvan maske su pregazeni";
}

// --- odnos rezolucija i dummy redovi -----------------------------------------

TEST_F(ScanRegistersTest, ResolutionRatioAndDummyLineRoundTrip) {
    auto s = scan();
    ASSERT_TRUE(s.setResolutionRatio(8));
    ASSERT_TRUE(s.setDummyLine(2));

    const auto ratio = s.resolutionRatio();
    ASSERT_TRUE(ratio);
    EXPECT_EQ(ratio.value(), 8);

    const auto dummy = s.dummyLine();
    ASSERT_TRUE(dummy);
    EXPECT_EQ(dummy.value(), 2);
}

TEST_F(ScanRegistersTest, ResolutionRatioAndDummyLineRejectOutOfRange) {
    auto s = scan();
    EXPECT_FALSE(s.setResolutionRatio(0));
    EXPECT_FALSE(s.setResolutionRatio(32));
    EXPECT_FALSE(s.setDummyLine(0));
    EXPECT_FALSE(s.setDummyLine(16));
}

// --- pokretanje --------------------------------------------------------------

// Sest upisa u tacnom redosledu. Skracivanje sekvence je najlaksi nacin da se
// dobije cip koji "ne pocinje", pa se broj i redosled drze zakljucanim.
TEST_F(ScanRegistersTest, ExecuteWritesTheExactReferenceSequence) {
    auto s = scan();
    recorder.clear();
    ASSERT_TRUE(s.execute());

    EXPECT_EQ(trace(),
              "CTL DI: c0 04 e800 0100 0002\n"
              "CTL DI: c0 04 e813 0100 0002\n"
              "CTL DI: c0 04 e814 0100 0002\n"
              "CTL DO: 40 04 e813 0000 0002\n"
              "CTL DI: c0 04 e801 0100 0002\n"
              "CTL DO: 40 04 e800 0000 0002\n"
              "CTL DI: c0 04 e814 0100 0002\n"
              "CTL DO: 40 04 e813 0000 0002\n"
              "CTL DI: c0 04 e801 0100 0002\n"
              "CTL DO: 40 04 e800 0000 0002\n"
              "CTL DI: c0 04 e801 0100 0002\n"
              "CTL DO: 40 04 e800 0000 0002\n");
}

TEST_F(ScanRegistersTest, ExecuteNeedsFullScanLevel) {
    auto s = scan(SafetyLevel::Acquire);
    const Status refused = s.execute();
    ASSERT_FALSE(refused);
    EXPECT_EQ(refused.error().code, ErrorCode::SafetyViolation);
}

TEST_F(ScanRegistersTest, WarmResetClearsTheExecutingBitToo) {
    auto s = scan();
    device.pokeRegister(reg::kControl, reg::kControlExecutingBit | 0x0F);
    ASSERT_TRUE(s.warmReset());

    const std::uint8_t value = device.peekRegister(reg::kControl);
    EXPECT_EQ(value & reg::kControlExecutingBit, 0) << "maska 0x3F obara i taj bit";
    EXPECT_EQ(value & reg::kControlWarmResetBit, 0) << "reset bit se posle spusta";
    EXPECT_EQ(value & 0x0F, 0x0F) << "nizi bitovi ostaju";
}

// Bit izvrsavanja se ne moze "postaviti" - on je stanje. Simulator ga drzi
// podignutim samo dok prolaz stvarno tece, kao i cip. Zato se ovde pokrece
// pravo skeniranje umesto da se registar upise rukom.
TEST_F(ScanRegistersTest, IsExecutingFollowsTheActualPass) {
    auto s = scan();
    const auto idle = s.isExecuting();
    ASSERT_TRUE(idle);
    EXPECT_FALSE(idle.value());

    ASSERT_TRUE(startSmallPass(s));

    const auto busy = s.isExecuting();
    ASSERT_TRUE(busy);
    EXPECT_TRUE(busy.value()) << "prolaz je pokrenut, cip skenira";
}

// Konfiguracija koju cip ne ume da protumaci ne pokrece nista, i bit odmah
// pada nazad - umesto da uredjaj ostane prividno zauzet zauvek.
TEST_F(ScanRegistersTest, ExecuteWithoutGeometryDoesNotLeaveTheChipBusy) {
    auto s = scan();
    ASSERT_TRUE(s.execute());

    const auto busy = s.isExecuting();
    ASSERT_TRUE(busy);
    EXPECT_FALSE(busy.value());
}

TEST_F(ScanRegistersTest, WaitScanEndReturnsImmediatelyWhenIdle) {
    auto s = scan();
    EXPECT_TRUE(s.waitScanEnd(std::chrono::milliseconds{0}));
}

// ODSTUPANJE OD REFERENCE, i zato eksplicitan test.
//
// RTS_WaitScanEnd vraca OK i kada rok istekne ("returns 0 if ok or timeout").
// Time se gubi razlika izmedju zavrsenog prolaza i odustajanja - bas ona koja
// se trazi u izvestaju sa tudjeg racunara.
TEST_F(ScanRegistersTest, WaitScanEndReportsTimeoutInsteadOfPretendingSuccess) {
    auto s = scan();
    ASSERT_TRUE(startSmallPass(s));

    // Prolaz tece, ali ga niko ne prazni - bit ostaje podignut.
    const Status result = s.waitScanEnd(std::chrono::milliseconds{5});
    ASSERT_FALSE(result) << "referenca bi ovde rekla OK";
    EXPECT_EQ(result.error().code, ErrorCode::Timeout);
}
