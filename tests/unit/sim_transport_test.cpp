#include "SimTransport.h"

#include "G2710Profile.generated.h"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <vector>

using namespace g2710;
using namespace g2710::sim;

namespace {

std::span<const std::byte> asBytes(const std::vector<std::uint8_t>& data) {
    return {reinterpret_cast<const std::byte*>(data.data()), data.size()};
}

}  // namespace

TEST(SimTransport, RegisterBankHasRealStateNotEcho) {
    // Ako bi simulator samo vracao ono sto mu se upise, Write_Byte
    // read-modify-write ciklus iz reference bi "prosao" i na neispravnoj
    // implementaciji. Zato bank mora imati stvarno stanje.
    SimTransport sim;

    const std::vector<std::uint8_t> written{0xAB, 0xCD};
    ASSERT_TRUE(sim.controlOut(0xE810, Command::RegisterWrite, asBytes(written)).hasValue());

    std::array<std::byte, 2> readBack{};
    ASSERT_TRUE(sim.controlIn(0xE810, Command::RegisterRead, readBack).hasValue());

    EXPECT_EQ(static_cast<std::uint8_t>(readBack[0]), 0xAB);
    EXPECT_EQ(static_cast<std::uint8_t>(readBack[1]), 0xCD);

    // Susedni registar ne sme biti dirnut.
    EXPECT_EQ(sim.peekRegister(0xE812), 0x00);
}

TEST(SimTransport, WriteByteReadModifyWriteTouchesTwoRegisters) {
    // Referenca upisuje JEDAN registarski bajt kroz DVA transfera:
    //   read  addr+1 (2 bajta, wIndex 0x100)
    //   write addr   (2 bajta, wIndex 0x000) sa [novi, stari]
    // Naivna implementacija kao jedan OUT transfer tiho korumpira susedni
    // registar - ovaj test opisuje tacno ocekivano ponasanje.
    SimTransport sim;
    sim.pokeRegister(0xE821, 0x5A);

    std::array<std::byte, 2> pair{};
    ASSERT_TRUE(sim.controlIn(0xE821, Command::RegisterRead, pair).hasValue());
    ASSERT_EQ(static_cast<std::uint8_t>(pair[0]), 0x5A);

    const std::vector<std::uint8_t> out{0x99, static_cast<std::uint8_t>(pair[0])};
    ASSERT_TRUE(sim.controlOut(0xE820, Command::RegisterWrite, asBytes(out)).hasValue());

    EXPECT_EQ(sim.peekRegister(0xE820), 0x99);
    EXPECT_EQ(sim.peekRegister(0xE821), 0x5A);
}

TEST(SimTransport, ChipsetResetClearsBankButKeepsChipsetId) {
    SimTransport sim;
    sim.pokeRegister(0xE850, 0x77);
    ASSERT_EQ(sim.peekRegister(0xE850), 0x77);

    ASSERT_TRUE(sim.controlOut(0x0000, Command::ChipsetReset, {}).hasValue());

    EXPECT_EQ(sim.chipsetResetCount(), 1);
    EXPECT_EQ(sim.peekRegister(0xE850), 0x00);
    EXPECT_EQ(sim.peekRegister(0xE800), profile::kChipsetModelId);
}

TEST(SimTransport, DmaEnableSizeIsInWordsNotBytes) {
    // RTS_DMA_Enable_* salje size/2 kao broj RECI. Zamena reci i bajtova je
    // greska koja bi se videla tek kao pola slike.
    SimTransport sim;

    const std::vector<std::uint8_t> enable{0, 0, 0, 0x10, 0x00, 0x00};  // 16 reci
    ASSERT_TRUE(sim.controlOut(0x0000, Command::DmaEnableRead, asBytes(enable)).hasValue());

    std::vector<std::byte> buffer(64);
    auto read = sim.bulkRead(buffer);
    ASSERT_TRUE(read.hasValue());
    EXPECT_EQ(read.value(), 32u) << "16 reci mora dati 32 bajta";
}

TEST(SimTransport, DmaEnableRejectsWrongBlockSize) {
    SimTransport sim;
    const std::vector<std::uint8_t> tooShort{0, 0, 0, 1};
    const Status status = sim.controlOut(0x0000, Command::DmaEnableWrite, asBytes(tooShort));
    ASSERT_FALSE(status.hasValue());
    EXPECT_EQ(status.error().code, ErrorCode::InvalidArgument);
}

TEST(SimTransport, PipeConfigurationMatchesReferenceEndpoints) {
    SimTransport sim;
    auto pipes = sim.pipeConfiguration();
    ASSERT_TRUE(pipes.hasValue());
    EXPECT_EQ(pipes.value().bulkIn, profile::kBulkInEndpoint);
    EXPECT_EQ(pipes.value().bulkOut, profile::kBulkOutEndpoint);
    EXPECT_TRUE(pipes.value().hasInterrupt) << "bez interrupt pipe-a nema dugmadi";
}

TEST(SimTransport, ButtonEventIsDeliveredOnceThenDrained) {
    SimTransport sim;
    sim.pressButton(profile::kButtons.mask[0]);

    auto first = sim.waitEvent();
    ASSERT_TRUE(first.hasValue());
    EXPECT_EQ(first.value(), static_cast<std::uint32_t>(profile::kButtons.mask[0]));

    auto second = sim.waitEvent();
    ASSERT_FALSE(second.hasValue());
    EXPECT_EQ(second.error().code, ErrorCode::Timeout);
}

TEST(SimTransport, CancelBlocksFurtherTransfersUntilReopen) {
    SimTransport sim;
    sim.cancel();

    std::array<std::byte, 2> buffer{};
    const Status afterCancel = sim.controlIn(0xE800, Command::RegisterRead, buffer);
    ASSERT_FALSE(afterCancel.hasValue());
    EXPECT_EQ(afterCancel.error().code, ErrorCode::Cancelled);

    ASSERT_TRUE(sim.reopen().hasValue());
    EXPECT_TRUE(sim.controlIn(0xE800, Command::RegisterRead, buffer).hasValue());
}

TEST(SimTransport, ReadThroughNonReadableCommandIsRejected) {
    // Referenca nikada ne cita kroz DMA/reset komande; tiho vracanje nula bi
    // sakrilo gresku u pozivu.
    SimTransport sim;
    std::array<std::byte, 2> buffer{};
    const Status status = sim.controlIn(0x0000, Command::DmaOpType, buffer);
    ASSERT_FALSE(status.hasValue());
    EXPECT_EQ(status.error().code, ErrorCode::InvalidArgument);
}

TEST(SimTransport, FullRegisterBankRoundTripsExceptHardwareBackedBytes) {
    // 1818 bajtova u jednom transferu - isto sto RTS_WriteRegs radi.
    //
    // Nekoliko registara NIJE memorija nego stanje hardvera: home senzor i
    // status lampi dolaze iz mehanike, ne iz onoga sto je upisano. Ni na
    // pravom uredjaju se kroz njih ne moze proci round-trip, pa se izuzimaju -
    // ocekivati suprotno znacilo bi testirati pogresnu stvar.
    SimTransport sim;
    const auto length = static_cast<std::size_t>(profile::kRegisterBankLength);

    std::vector<std::uint8_t> pattern(length);
    for (std::size_t i = 0; i < length; ++i) {
        pattern[i] = static_cast<std::uint8_t>(i & 0xFF);
    }

    ASSERT_TRUE(sim.controlOut(static_cast<std::uint16_t>(profile::kRegisterBankBase),
                               Command::RegisterWrite, asBytes(pattern)).hasValue());

    std::vector<std::byte> readBack(length);
    ASSERT_TRUE(sim.controlIn(static_cast<std::uint16_t>(profile::kRegisterBankBase),
                              Command::RegisterRead, readBack).hasValue());

    int skipped = 0;
    for (std::size_t i = 0; i < length; ++i) {
        const auto address =
            static_cast<std::uint16_t>(profile::kRegisterBankBase + i);
        if (SimTransport::isHardwareBackedRegister(address)) {
            ++skipped;
            continue;
        }
        ASSERT_EQ(static_cast<std::uint8_t>(readBack[i]), pattern[i])
            << "razlika na offsetu " << i;
    }
    EXPECT_GT(skipped, 0) << "nijedan registar nije hardverski - preslikavanje ne radi";
}
