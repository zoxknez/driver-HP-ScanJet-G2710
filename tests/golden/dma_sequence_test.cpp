// Golden sekvence DMA sloja.
//
// Izvedeno iz rts8822.c:4234 RTS_DMA_Read i :4266 RTS_DMA_Write.
//
// Najvazniji deo je verifikaciona petlja upisa: referenca svaki DMA upis cita
// nazad i uporedjuje, do deset puta. To se ne radi bez razloga, pa se ponasanje
// prenosi doslovno - i testira injektovanjem greske, da petlja ne bi tiho
// prolazila iz prvog pokusaja.

#include "SimTransport.h"
#include "rts8822/Dma.h"
#include "rts8822/RegisterFile.h"
#include "transport/TraceRecorder.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <string>
#include <vector>

using namespace g2710;
using namespace g2710::rts8822;

namespace {

class DmaSequence : public ::testing::Test {
protected:
    sim::SimTransport device;
    TraceRecorder recorder{device};
    RegisterFile registers{recorder};

    Dma dma() { return Dma{registers, SafetyGate{SafetyLevel::FullScan}}; }
    std::string trace() const { return recorder.format(); }

    static std::vector<std::byte> pattern(std::size_t size) {
        std::vector<std::byte> data(size);
        for (std::size_t i = 0; i < size; ++i) {
            data[i] = static_cast<std::byte>((i * 7 + 1) & 0xFF);
        }
        return data;
    }
};

}  // namespace

// rts8822.c:4290 RTS_DMA_Reset -> IWrite_Word(0x0000, 0x0000, 0x0800)
TEST_F(DmaSequence, ResetIsOperationTypeZero) {
    auto d = dma();
    ASSERT_TRUE(d.reset().hasValue());

    EXPECT_EQ(trace(), "CTL DO: 40 04 0000 0800 0002\n");
    EXPECT_EQ(device.dmaOperationType(), 0x0000);
}

// rts8822.c:6226 koristi tip operacije 0x0014 za shading upload.
TEST_F(DmaSequence, OperationTypeIsCarriedThrough) {
    auto d = dma();
    ASSERT_TRUE(d.setOperationType(0x0014).hasValue());
    EXPECT_EQ(device.dmaOperationType(), 0x0014);
}

// rts8822.c:4278 RTS_DMA_Cancel -> IWrite_Word(0x0000, 0, 0x0600)
TEST_F(DmaSequence, CancelUsesItsOwnCommand) {
    auto d = dma();
    ASSERT_TRUE(d.cancel().hasValue());
    EXPECT_EQ(trace(), "CTL DO: 40 04 0000 0600 0002\n");
}

// rts8822.c:4234 RTS_DMA_Read: reset -> enable read -> bulk read
TEST_F(DmaSequence, ReadIsResetThenEnableThenBulk) {
    auto d = dma();
    std::vector<std::byte> buffer(32);
    ASSERT_TRUE(d.read(0x0004, 0x000000, buffer).hasValue());

    EXPECT_EQ(trace(),
              "CTL DO: 40 04 0000 0800 0002\n"   // reset
              "CTL DO: 40 04 0004 0400 0006\n"   // enable read, 6 bajtova
              "BLK DI: 32 bytes\n");
}

TEST_F(DmaSequence, EnableBlockCarriesWordsLsbAndOptionsMsb) {
    // 6 bajtova: options MSB-first [0..2], broj RECI LSB-first [3..5].
    // Zamena reci i bajtova daje pola slike.
    auto d = dma();
    std::vector<std::byte> buffer(512);
    ASSERT_TRUE(d.read(0x0004, 0x123456, buffer).hasValue());

    const auto& entries = recorder.entries();
    ASSERT_GE(entries.size(), 2u);
    const auto& block = entries[1].data;
    ASSERT_EQ(block.size(), 6u);

    EXPECT_EQ(block[0], 0x12) << "options MSB-first";
    EXPECT_EQ(block[1], 0x34);
    EXPECT_EQ(block[2], 0x56);

    EXPECT_EQ(block[3], 0x00) << "256 reci LSB-first, ne 512 bajtova";
    EXPECT_EQ(block[4], 0x01);
    EXPECT_EQ(block[5], 0x00);
}

// rts8822.c:4266 RTS_DMA_Write: reset -> enable write -> [write, enable read,
// read back, compare] dok se ne poklopi
TEST_F(DmaSequence, WriteVerifiesByReadingBack) {
    auto d = dma();
    const auto data = pattern(16);
    ASSERT_TRUE(d.write(0x0004, 0x000000, data).hasValue());

    EXPECT_EQ(trace(),
              "CTL DO: 40 04 0000 0800 0002\n"   // reset
              "CTL DO: 40 04 0004 0401 0006\n"   // enable write
              "BLK DO: 16 bytes\n"               // upis
              "CTL DO: 40 04 0004 0400 0006\n"   // enable read radi provere
              "BLK DI: 16 bytes\n");             // citanje nazad

    EXPECT_EQ(d.lastWriteAttempts(), 1);
}

TEST_F(DmaSequence, WriteRetriesWhenReadBackDiffers) {
    // Bez ovoga bi verifikaciona petlja bila mrtav kod: sve bi prolazilo iz
    // prvog pokusaja i niko ne bi primetio da ponavljanje ne radi.
    auto d = dma();
    device.corruptNextDmaReadBacks(2);

    const auto data = pattern(16);
    ASSERT_TRUE(d.write(0x0004, 0x000000, data).hasValue());

    EXPECT_EQ(d.lastWriteAttempts(), 3) << "dva pokvarena citanja -> treci pokusaj uspeva";

    // Posle neuspele provere referenca otkazuje pa ponovo omogucava upis.
    const std::string t = trace();
    EXPECT_NE(t.find("CTL DO: 40 04 0000 0600 0002\n"), std::string::npos)
        << "izostao je DMA cancel izmedju pokusaja";
}

TEST_F(DmaSequence, WriteGivesUpAfterTenAttempts) {
    auto d = dma();
    device.corruptNextDmaReadBacks(100);

    const auto data = pattern(16);
    const Status status = d.write(0x0004, 0x000000, data);

    ASSERT_FALSE(status.hasValue());
    EXPECT_EQ(status.error().code, ErrorCode::DeviceError);
    EXPECT_EQ(d.lastWriteAttempts(), kDmaWriteAttempts);
}

TEST_F(DmaSequence, BulkIsChunkedByTransferSize) {
    auto d = dma();
    d.setTransferSize(8);

    std::vector<std::byte> buffer(32);
    ASSERT_TRUE(d.read(0x0004, 0, buffer).hasValue());

    EXPECT_EQ(trace(),
              "CTL DO: 40 04 0000 0800 0002\n"
              "CTL DO: 40 04 0004 0400 0006\n"
              "BLK DI: 8 bytes\n"
              "BLK DI: 8 bytes\n"
              "BLK DI: 8 bytes\n"
              "BLK DI: 8 bytes\n");
}

TEST_F(DmaSequence, DefaultTransferSizeMatchesReference) {
    auto d = dma();
    EXPECT_EQ(d.transferSize(), kDefaultDmaTransferSize);
    EXPECT_EQ(kDefaultDmaTransferSize, 0x80000u) << "rts8822.c:14015";
}

TEST_F(DmaSequence, EmptyBufferIsRejectedNotSilentlyIgnored) {
    auto d = dma();
    const Status status = d.read(0x0004, 0, {});
    ASSERT_FALSE(status.hasValue());
    EXPECT_EQ(status.error().code, ErrorCode::InvalidArgument);
}

TEST_F(DmaSequence, WaitReadyPollsDmaStatusBit) {
    auto d = dma();
    PollPolicy policy;
    policy.interval = std::chrono::milliseconds{0};
    ASSERT_TRUE(d.waitReady(policy).hasValue());

    EXPECT_EQ(trace(), "CTL DI: c0 04 ef09 0100 0002\n");
}

TEST_F(DmaSequence, WaitReadyTimesOutWhenBitNeverSets) {
    device.pokeRegister(0xEF09, 0x00);

    auto d = dma();
    PollPolicy policy;
    policy.timeout = std::chrono::milliseconds{0};
    policy.interval = std::chrono::milliseconds{0};

    const Status status = d.waitReady(policy);
    ASSERT_FALSE(status.hasValue());
    EXPECT_EQ(status.error().code, ErrorCode::Timeout);
}

// --- bezbednosne kapije -----------------------------------------------------

TEST_F(DmaSequence, TransfersRequireAcquireLevel) {
    Dma restricted{registers, SafetyGate{SafetyLevel::Lamp}};

    std::vector<std::byte> buffer(16);
    const Status read = restricted.read(0x0004, 0, buffer);
    ASSERT_FALSE(read.hasValue());
    EXPECT_EQ(read.error().code, ErrorCode::SafetyViolation);

    const auto data = pattern(16);
    const Status write = restricted.write(0x0004, 0, data);
    ASSERT_FALSE(write.hasValue());
    EXPECT_EQ(write.error().code, ErrorCode::SafetyViolation);

    // Upravljanje je nivo 2 i ostaje dozvoljeno.
    EXPECT_TRUE(restricted.reset().hasValue());
    EXPECT_TRUE(restricted.cancel().hasValue());
}

TEST_F(DmaSequence, NoTransferIsIssuedWhenSafetyRefuses) {
    Dma restricted{registers, SafetyGate{SafetyLevel::Lamp}};
    std::vector<std::byte> buffer(16);
    (void)restricted.read(0x0004, 0, buffer);

    EXPECT_TRUE(trace().empty())
        << "odbijena operacija je ipak poslala nesto uredjaju";
}
