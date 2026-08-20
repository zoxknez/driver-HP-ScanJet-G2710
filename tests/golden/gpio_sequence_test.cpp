// Golden sekvence GPIO sloja.
//
// Izvedeno iz rts8822.c:764 SetLock, :792 Set_E950_Mode i :1070 RTS_Sensor_Type.

#include "G2710Profile.generated.h"
#include "SimTransport.h"
#include "rts8822/Gpio.h"
#include "rts8822/RegisterFile.h"
#include "rts8822/Registers.h"
#include "transport/TraceRecorder.h"

#include <gtest/gtest.h>

#include <chrono>
#include <string>

using namespace g2710;
using namespace g2710::rts8822;

namespace {

constexpr std::chrono::milliseconds kNoWait{0};

class GpioSequence : public ::testing::Test {
protected:
    sim::SimTransport device;
    TraceRecorder recorder{device};
    RegisterFile registers{recorder};

    Gpio gpio() { return Gpio{registers, SafetyGate{SafetyLevel::FullScan}}; }
    std::string trace() const { return recorder.format(); }
};

}  // namespace

// SetLock cita bajt pa ga upisuje; Write_Byte je read-modify-write, pa je
// ukupno tri transfera.
TEST_F(GpioSequence, SetLockIsReadThenWriteByte) {
    auto g = gpio();
    ASSERT_TRUE(g.setLock(true).hasValue());

    EXPECT_EQ(trace(),
              "CTL DI: c0 04 ee00 0100 0002\n"
              "CTL DI: c0 04 ee01 0100 0002\n"
              "CTL DO: 40 04 ee00 0000 0002\n");
}

TEST_F(GpioSequence, LockTouchesOnlyItsBit) {
    device.pokeRegister(reg::kLock, 0xFB);  // sve osim bita 2

    auto g = gpio();
    ASSERT_TRUE(g.setLock(true).hasValue());
    EXPECT_EQ(device.peekRegister(reg::kLock), 0xFF);

    ASSERT_TRUE(g.setLock(false).hasValue());
    EXPECT_EQ(device.peekRegister(reg::kLock), 0xFB);
}

// Set_E950_Mode cita REC i upisuje REC - za razliku od Motor_Change, koji
// cita rec a upisuje bajt. Razlika je stvarna i vidi se u sekvenci.
TEST_F(GpioSequence, E950ModeReadsAndWritesAWordNotAByte) {
    auto g = gpio();
    ASSERT_TRUE(g.setE950Mode(true).hasValue());

    EXPECT_EQ(trace(),
              "CTL DI: c0 04 e950 0100 0002\n"
              "CTL DO: 40 04 e950 0000 0002\n")
        << "dva transfera znace Write_Word; tri bi znacila Write_Byte";
}

TEST_F(GpioSequence, E950ModeClearsWithMaskFromReference) {
    // data & 0xffbf pri gasenju, data | 0x40 pri paljenju.
    device.pokeRegister(reg::kGpio0, 0xFF);
    device.pokeRegister(static_cast<std::uint16_t>(reg::kGpio0 + 1), 0xFF);

    auto g = gpio();
    ASSERT_TRUE(g.setE950Mode(false).hasValue());

    EXPECT_EQ(device.peekRegister(reg::kGpio0) & 0x40, 0x00);
    EXPECT_EQ(device.peekRegister(reg::kGpio0) & 0xBF, 0xBF) << "ostali bitovi ostaju";
    EXPECT_EQ(device.peekRegister(static_cast<std::uint16_t>(reg::kGpio0 + 1)), 0xFF)
        << "visi bajt reci je pregazen";
}

// --- detekcija senzora -------------------------------------------------------

TEST_F(GpioSequence, SensorDetectSavesProbesAndRestores) {
    auto g = gpio();
    ASSERT_TRUE(g.detectSensorType(kNoWait).hasValue());

    EXPECT_EQ(trace(),
              "CTL DI: c0 04 e950 0100 0002\n"   // sacuvaj GPIO0
              "CTL DI: c0 04 e956 0100 0002\n"   // sacuvaj GPIO1
              "CTL DO: 40 04 e950 0000 0002\n"   // probna vrednost 0x13ff
              "CTL DO: 40 04 e956 0000 0002\n"   // probna vrednost 0xfcf0
              "CTL DI: c0 04 e968 0100 0002\n"   // procitaj senzor
              "CTL DO: 40 04 e950 0000 0002\n"   // vrati GPIO0
              "CTL DO: 40 04 e956 0000 0002\n"); // vrati GPIO1
}

TEST_F(GpioSequence, SensorDetectLeavesGpioExactlyAsItFoundIt) {
    // Ovo je poenta cuvanja i vracanja: detekcija ne sme promeniti stanje
    // uredjaja, jer se poziva pri inicijalizaciji.
    device.pokeRegister(reg::kGpio0, 0xA1);
    device.pokeRegister(static_cast<std::uint16_t>(reg::kGpio0 + 1), 0xB2);
    device.pokeRegister(reg::kGpio1, 0xC3);
    device.pokeRegister(static_cast<std::uint16_t>(reg::kGpio1 + 1), 0xD4);

    auto g = gpio();
    ASSERT_TRUE(g.detectSensorType(kNoWait).hasValue());

    EXPECT_EQ(device.peekRegister(reg::kGpio0), 0xA1);
    EXPECT_EQ(device.peekRegister(static_cast<std::uint16_t>(reg::kGpio0 + 1)), 0xB2);
    EXPECT_EQ(device.peekRegister(reg::kGpio1), 0xC3);
    EXPECT_EQ(device.peekRegister(static_cast<std::uint16_t>(reg::kGpio1 + 1)), 0xD4);
}

TEST_F(GpioSequence, SensorDetectWritesTheProbeValuesFromReference) {
    auto g = gpio();
    ASSERT_TRUE(g.detectSensorType(kNoWait).hasValue());

    const auto& entries = recorder.entries();
    ASSERT_GE(entries.size(), 4u);

    // 0x13ff na GPIO0, little-endian.
    EXPECT_EQ(entries[2].data.at(0), 0xFF);
    EXPECT_EQ(entries[2].data.at(1), 0x13);

    // 0xfcf0 na GPIO1.
    EXPECT_EQ(entries[3].data.at(0), 0xF0);
    EXPECT_EQ(entries[3].data.at(1), 0xFC);
}

TEST_F(GpioSequence, ClearBitEightMeansCcd) {
    // rst = ((_B1(c) & 1) == 0) ? CCD_SENSOR : CIS_SENSOR
    device.pokeRegister(static_cast<std::uint16_t>(reg::kGpioSense + 1), 0x00);

    auto g = gpio();
    auto type = g.detectSensorType(kNoWait);
    ASSERT_TRUE(type.hasValue());
    EXPECT_EQ(type.value(), SensorType::Ccd);
}

TEST_F(GpioSequence, SetBitEightMeansCis) {
    device.pokeRegister(static_cast<std::uint16_t>(reg::kGpioSense + 1), 0x01);

    auto g = gpio();
    auto type = g.detectSensorType(kNoWait);
    ASSERT_TRUE(type.hasValue());
    EXPECT_EQ(type.value(), SensorType::Cis);
}

TEST_F(GpioSequence, DetectedTypeAgreesWithExtractedProfile) {
    // Profil kaze CCD_SENSOR (cfg_sensor_get). Ako bi detekcija na pravom
    // uredjaju vratila CIS, jedno od to dvoje je pogresno - i to je nesto sto
    // H2 mora da prijavi, ne da progura.
    ASSERT_EQ(profile::kSensor.type, static_cast<int>(SensorType::Ccd));

    auto g = gpio();
    auto type = g.detectSensorType(kNoWait);
    ASSERT_TRUE(type.hasValue());
    EXPECT_EQ(static_cast<int>(type.value()), profile::kSensor.type);
}

TEST_F(GpioSequence, RequiresLampLevel) {
    Gpio restricted{registers, SafetyGate{SafetyLevel::ReadOnly}};

    EXPECT_EQ(restricted.setLock(true).error().code, ErrorCode::SafetyViolation);
    EXPECT_EQ(restricted.setE950Mode(true).error().code, ErrorCode::SafetyViolation);
    EXPECT_EQ(restricted.detectSensorType(kNoWait).error().code, ErrorCode::SafetyViolation);

    EXPECT_TRUE(trace().empty()) << "odbijena operacija je ipak nesto poslala";
}
