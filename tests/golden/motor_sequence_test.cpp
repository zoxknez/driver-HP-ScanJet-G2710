// Golden sekvence motornog sloja.
//
// Izvedeno iz rts8822.c:4197 Motor_Change i :5137 Motor_GetFromResolution.

#include "SimTransport.h"
#include "rts8822/Motor.h"
#include "rts8822/RegisterFile.h"
#include "rts8822/Registers.h"
#include "transport/TraceRecorder.h"

#include <gtest/gtest.h>

#include <string>

using namespace g2710;
using namespace g2710::rts8822;

// --- izbor struje po rezoluciji ---------------------------------------------
// Ovo su constexpr provere: ne trebaju ni simulator ni transport.

static_assert(motorCurrentForResolution(300, ScanType::Normal, UsbSpeed::Usb20) == 3);
static_assert(motorCurrentForResolution(600, ScanType::Normal, UsbSpeed::Usb20) == 3,
              "flatbed na USB 2.0 prelazi tek na 1200, ne na 600");
static_assert(motorCurrentForResolution(1200, ScanType::Normal, UsbSpeed::Usb20) == 0);
static_assert(motorCurrentForResolution(2400, ScanType::Normal, UsbSpeed::Usb20) == 0);

static_assert(motorCurrentForResolution(300, ScanType::Tma, UsbSpeed::Usb20) == 3);
static_assert(motorCurrentForResolution(600, ScanType::Tma, UsbSpeed::Usb20) == 0,
              "TMA prelazi vec na 600");
static_assert(motorCurrentForResolution(600, ScanType::Negative, UsbSpeed::Usb20) == 0);

static_assert(motorCurrentForResolution(300, ScanType::Normal, UsbSpeed::Usb11) == 3);
static_assert(motorCurrentForResolution(600, ScanType::Normal, UsbSpeed::Usb11) == 0,
              "na USB 1.1 i flatbed prelazi na 600");

// --- mapiranje vrednosti u bitove -------------------------------------------

static_assert(motorCurrentBits(3) == 0x30);
static_assert(motorCurrentBits(2) == 0x20);
static_assert(motorCurrentBits(1) == 0x10);

// Referenca radi `value--` nad bajtom, pa 0 postaje 255 i ne pogadja nijedan
// case. Bitovi ostaju obrisani - to je nacin izbora, ne previd.
static_assert(motorCurrentBits(0) == 0x00,
              "vrednost 0 mora obrisati bitove struje, ne postaviti 0x10");

#if G2710_MOTOR_PATH_COMPILED

namespace {

class MotorSequence : public ::testing::Test {
protected:
    sim::SimTransport device;
    TraceRecorder recorder{device};
    RegisterFile registers{recorder};

    Motor motor() { return Motor{registers, SafetyGate{SafetyLevel::FullScan}}; }
    std::string trace() const { return recorder.format(); }
};

}  // namespace

// Read_Word(0xe954) pa Write_Byte(0xe954). Write_Byte je read-modify-write,
// pa je ukupno tri transfera, ne dva.
TEST_F(MotorSequence, ApplyMotorCurrentIsReadWordThenWriteByte) {
    auto m = motor();
    ASSERT_TRUE(m.applyMotorCurrent(3).hasValue());

    EXPECT_EQ(trace(),
              "CTL DI: c0 04 e954 0100 0002\n"   // Read_Word
              "CTL DI: c0 04 e955 0100 0002\n"   // Write_Byte faza 1
              "CTL DO: 40 04 e954 0000 0002\n"); // Write_Byte faza 2
}

TEST_F(MotorSequence, CurrentBitsLandInTheMaskAndNothingElseMoves) {
    device.pokeRegister(reg::kLampMode, 0xCF);  // sve van maske 0x30 postavljeno

    auto m = motor();
    ASSERT_TRUE(m.applyMotorCurrent(2).hasValue());

    const std::uint8_t value = device.peekRegister(reg::kLampMode);
    EXPECT_EQ(value & 0x30, 0x20) << "bitovi struje";
    EXPECT_EQ(value & 0xCF, 0xCF) << "bitovi van maske su pregazeni";
}

TEST_F(MotorSequence, ValueZeroClearsCurrentBits) {
    device.pokeRegister(reg::kLampMode, 0x30);

    auto m = motor();
    ASSERT_TRUE(m.applyMotorCurrent(0).hasValue());

    EXPECT_EQ(device.peekRegister(reg::kLampMode) & 0x30, 0x00);
}

TEST_F(MotorSequence, ResolutionPathMatchesDirectValue) {
    auto m = motor();
    ASSERT_TRUE(m.applyMotorCurrentForResolution(300, ScanType::Normal,
                                                 UsbSpeed::Usb20).hasValue());
    EXPECT_EQ(device.peekRegister(reg::kLampMode) & 0x30, 0x30);

    ASSERT_TRUE(m.applyMotorCurrentForResolution(1200, ScanType::Normal,
                                                 UsbSpeed::Usb20).hasValue());
    EXPECT_EQ(device.peekRegister(reg::kLampMode) & 0x30, 0x00);
}

TEST_F(MotorSequence, RequiresMotorLevel) {
    Motor restricted{registers, SafetyGate{SafetyLevel::Lamp}};
    const Status status = restricted.applyMotorCurrent(3);

    ASSERT_FALSE(status.hasValue());
    EXPECT_EQ(status.error().code, ErrorCode::SafetyViolation);
    EXPECT_TRUE(trace().empty()) << "odbijena operacija je ipak nesto poslala";
}

#endif  // G2710_MOTOR_PATH_COMPILED
