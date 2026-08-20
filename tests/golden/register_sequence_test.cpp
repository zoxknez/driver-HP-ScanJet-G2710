// Golden register sequence testovi.
//
// Svaka operacija mora proizvesti TACNO onu sekvencu transfera koju referenca
// proizvodi. Ocekivane sekvence su izvedene citanjem hp3900_rts8822.c i
// hp3900_usb.c, sa navedenim linijama - nisu snimljene sa hardvera, jer je
// USB capture svesno odlozen kao kontingencija.
//
// Format je isti kao debug izlaz reference, pa se ovi stringovi mogu direktno
// uporediti sa `SANE_DEBUG_HP3900=255` izlazom ako ikada zatreba poredjenje sa
// referentnom implementacijom u radu.
//
// Ovo su najvredniji testovi u fazi G2710-2: hvataju greske u BROJU i
// REDOSLEDU transfera, koje se inace vide tek na hardveru.

#include "SimTransport.h"
#include "rts8822/Registers.h"
#include "rts8822/Rts8822.h"
#include "transport/TraceRecorder.h"

#include <gtest/gtest.h>

#include <string>

using namespace g2710;
using namespace g2710::rts8822;

namespace {

class GoldenSequence : public ::testing::Test {
protected:
    sim::SimTransport device;
    TraceRecorder recorder{device};

    Rts8822 chip() { return Rts8822{recorder, SafetyGate{SafetyLevel::FullScan}}; }
    std::string trace() const { return recorder.format(); }
};

}  // namespace

// rts8822.c:3805 Head_IsAtHome -> Read_Byte(0xe96f)
TEST_F(GoldenSequence, IsHeadAtHomeIsOneRead) {
    auto rts = chip();
    ASSERT_TRUE(rts.isHeadAtHome().hasValue());

    EXPECT_EQ(trace(), "CTL DI: c0 04 e96f 0100 0002\n");
}

// rts8822.c:3832 RTS_IsExecuting -> Read_Byte(0xe800)
TEST_F(GoldenSequence, IsExecutingIsOneRead) {
    auto rts = chip();
    ASSERT_TRUE(rts.isExecuting().hasValue());

    EXPECT_EQ(trace(), "CTL DI: c0 04 e800 0100 0002\n");
}

// rts8822.c:4104 Lamp_Status_Get -> Read_Byte(0xe946) pa Read_Word(0xe954)
TEST_F(GoldenSequence, LampStatusReadsStatusThenMode) {
    auto rts = chip();
    ASSERT_TRUE(rts.lampStatus().hasValue());

    EXPECT_EQ(trace(),
              "CTL DI: c0 04 e946 0100 0002\n"
              "CTL DI: c0 04 e954 0100 0002\n");
}

// rts8822.c:2519 Lamp_PWM_DutyCycle_Get -> Read_Byte(0xe948)
TEST_F(GoldenSequence, LampPwmDutyCycleIsOneRead) {
    auto rts = chip();
    ASSERT_TRUE(rts.lampPwmDutyCycle().hasValue());

    EXPECT_EQ(trace(), "CTL DI: c0 04 e948 0100 0002\n");
}

// NAJVAZNIJI TEST U OVOM FAJLU.
//
// rts8822.c:3905 RTS_Warm_Reset izgleda kao tri operacije - jedno citanje i
// dva upisa. Ali svaki Write_Byte je read-modify-write (usb.c:108), pa je
// stvarnost PET transfera, a upisi citaju SLEDECU adresu (0xe801) sa
// wIndex 0x0100 pa upisuju trazenu (0xe800) sa wIndex 0x0000.
//
// Implementacija koja upise jedan OUT transfer po Write_Byte dala bi tri
// transfera i tiho korumpirala registar 0xe801.
TEST_F(GoldenSequence, WarmResetIsFiveTransfersNotThree) {
    auto rts = chip();
    ASSERT_TRUE(rts.warmReset().hasValue());

    EXPECT_EQ(trace(),
              "CTL DI: c0 04 e800 0100 0002\n"   // procitaj kControl
              "CTL DI: c0 04 e801 0100 0002\n"   // writeByte faza 1
              "CTL DO: 40 04 e800 0000 0002\n"   // writeByte faza 2, bit postavljen
              "CTL DI: c0 04 e801 0100 0002\n"   // writeByte faza 1
              "CTL DO: 40 04 e800 0000 0002\n"); // writeByte faza 2, bit obrisan
}

TEST_F(GoldenSequence, WarmResetAssertsThenReleasesTheBit) {
    // Sekvenca je tacna; sada i vrednosti. Bit 6 mora prvo biti postavljen pa
    // obrisan - obrnut redosled ne bi resetovao nista.
    auto rts = chip();
    ASSERT_TRUE(rts.warmReset().hasValue());

    const auto& entries = recorder.entries();
    ASSERT_EQ(entries.size(), 5u);

    const auto asserted = entries[2].data.at(0);
    const auto released = entries[4].data.at(0);

    EXPECT_NE(asserted & reg::kControlWarmResetBit, 0)
        << "prvi upis mora POSTAVITI warm reset bit";
    EXPECT_EQ(released & reg::kControlWarmResetBit, 0)
        << "drugi upis mora OBRISATI warm reset bit";
}

// rts8822.c:4222 Chipset_Reset -> IWrite_Buffer(0x0000, NULL, 0, 0x0801)
TEST_F(GoldenSequence, ChipsetResetIsOneEmptyCommand) {
    auto rts = chip();
    ASSERT_TRUE(rts.chipsetReset().hasValue());

    EXPECT_EQ(trace(), "CTL DO: 40 04 0000 0801 0000\n");
    EXPECT_EQ(device.chipsetResetCount(), 1);
}

// rts8822.c:3884 RTS_Enable_CCD -> Read_Buffer(0xe810,4) pa Write_Buffer(0xe810,4)
TEST_F(GoldenSequence, EnableCcdChannelsIsReadModifyWriteOfFourBytes) {
    auto rts = chip();
    ASSERT_TRUE(rts.enableCcdChannels(0x07).hasValue());

    EXPECT_EQ(trace(),
              "CTL DI: c0 04 e810 0100 0004\n"
              "CTL DO: 40 04 e810 0000 0004\n");
}

TEST_F(GoldenSequence, EnableCcdChannelsSplitsBitsAcrossTwoRegisters) {
    // Donja tri bita idu u masku 0xE0 registra 0x010, cetvrti u masku 0x80
    // registra 0x013. Slanje svih cetiri u isti registar bi tiho iskljucilo
    // jedan kanal.
    auto rts = chip();
    ASSERT_TRUE(rts.enableCcdChannels(0x0F).hasValue());

    EXPECT_EQ(bitsetGet(device.peekRegister(0xE810), reg::kCcdChannelsLowMask), 0x07);
    EXPECT_EQ(bitsetGet(device.peekRegister(0xE813), reg::kCcdChannelsHighMask), 0x01);
}

TEST_F(GoldenSequence, EnableCcdChannelsPreservesUnrelatedBits) {
    device.pokeRegister(0xE810, 0x1F);  // bitovi van maske 0xE0
    device.pokeRegister(0xE813, 0x7F);  // bitovi van maske 0x80

    auto rts = chip();
    ASSERT_TRUE(rts.enableCcdChannels(0x0F).hasValue());

    EXPECT_EQ(device.peekRegister(0xE810) & 0x1F, 0x1F)
        << "read-modify-write je pregazio bitove van maske";
    EXPECT_EQ(device.peekRegister(0xE813) & 0x7F, 0x7F)
        << "read-modify-write je pregazio bitove van maske";
}

// --- bitfield pomocne funkcije ---------------------------------------------

TEST(Bitset, ValueIsAlignedToLowestMaskBit) {
    EXPECT_EQ(bitsetValue(0x00, 0xE0, 0x07), 0xE0);
    EXPECT_EQ(bitsetValue(0x00, 0xE0, 0x05), 0xA0);
    EXPECT_EQ(bitsetValue(0x00, 0x3F, 0x2A), 0x2A);
    EXPECT_EQ(bitsetValue(0x00, 0x80, 0x01), 0x80);
}

TEST(Bitset, PreservesBitsOutsideMask) {
    EXPECT_EQ(bitsetValue(0x1F, 0xE0, 0x07), 0xFF);
    EXPECT_EQ(bitsetValue(0xFF, 0xE0, 0x00), 0x1F);
}

TEST(Bitset, GetIsInverseOfValue) {
    for (std::uint8_t mask : {std::uint8_t{0xE0}, std::uint8_t{0x3F}, std::uint8_t{0x80}}) {
        const std::uint8_t width = static_cast<std::uint8_t>(bitsetGet(mask, mask));
        for (std::uint8_t v = 0; v <= width; ++v) {
            EXPECT_EQ(bitsetGet(bitsetValue(0x00, mask, v), mask), v);
        }
    }
}

TEST(Bitset, ZeroMaskIsNoOp) {
    EXPECT_EQ(bitsetValue(0xAB, 0x00, 0xFF), 0xAB);
    EXPECT_EQ(bitsetGet(0xAB, 0x00), 0x00);
}
