// RTS8822 DMA sloj.
//
// Kanonska sekvenca iz reference (rts8822.c:4234 RTS_DMA_Read):
//
//   reset          IWrite_Word(0x0000, 0x0000, wIndex 0x0800)
//   enable read    IWrite_Buffer(dmacs, 6 bajtova, wIndex 0x0400)
//   bulk read      Read_Bulk u komadima od dmatransfersize
//
// Upis (rts8822.c:4266 RTS_DMA_Write) je slozeniji i to nije slucajno: svaki
// upis se VERIFIKUJE citanjem nazad, sa do deset pokusaja. Referenca to ne bi
// radila da je putanja pouzdana, pa se ponasanje prenosi doslovno.

#pragma once

#include "../device/SafetyLevel.h"
#include "RegisterFile.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <span>

namespace g2710::rts8822 {

// rts8822.c:14015 RTS_Debug->dmatransfersize = 0x80000
inline constexpr std::size_t kDefaultDmaTransferSize = 0x80000;  // 512 KB

// rts8822.c:4292 - `for (a = 10; a > 0; a--)`
inline constexpr int kDmaWriteAttempts = 10;

struct PollPolicy {
    std::chrono::milliseconds timeout{10000};
    // Referenca spava 100 ms izmedju anketa (usleep(1000 * 100)).
    std::chrono::milliseconds interval{100};
};

class Dma {
public:
    Dma(RegisterFile& registers, SafetyGate gate) : registers_(registers), gate_(gate) {}

    // --- nivo 2: upravljanje ---------------------------------------------

    // rts8822.c:4290 RTS_DMA_Reset -> operacija tipa 0 preko wIndex 0x0800
    Status reset();

    // Bira tip DMA operacije. rts8822.c:6226 koristi 0x0014 za shading upload.
    Status setOperationType(std::uint16_t operationType);

    // rts8822.c:4278 RTS_DMA_Cancel
    Status cancel();

    // rts8822.c:4138 RTS_DMA_WaitReady - anketira kDmaStatus bit 0
    Status waitReady(const PollPolicy& policy = {});

    // --- nivo 4: prenos podataka ------------------------------------------

    Status read(std::uint16_t dmacs, std::uint32_t options, std::span<std::byte> buffer);

    // Upisuje pa cita nazad i uporedjuje, do kDmaWriteAttempts puta. Vraca
    // DeviceError ako se ni posle svih pokusaja bafer ne poklopi.
    Status write(std::uint16_t dmacs, std::uint32_t options,
                 std::span<const std::byte> buffer);

    // Velicina komada za bulk prenos. Izlozeno jer H5 treba da proveri da li
    // usbscan.sys podnosi 512 KB odjednom.
    void setTransferSize(std::size_t bytes) noexcept;
    std::size_t transferSize() const noexcept { return transferSize_; }

    // Broj pokusaja upisa u poslednjem write() - dijagnostika. Vrednost veca
    // od 1 znaci da je putanja podataka pogresila i sama se ispravila, sto je
    // podatak koji treba da stigne u H5 izvestaj, a ne da se izgubi.
    int lastWriteAttempts() const noexcept { return lastWriteAttempts_; }

private:
    Status enableRead(std::uint16_t dmacs, std::uint32_t options, std::size_t size);
    Status enableWrite(std::uint16_t dmacs, std::uint32_t options, std::size_t size);
    Status enable(Command command, std::uint16_t dmacs, std::uint32_t options,
                  std::size_t size);

    Status bulkReadAll(std::span<std::byte> buffer);
    Status bulkWriteAll(std::span<const std::byte> buffer);

    RegisterFile& registers_;
    SafetyGate gate_;
    std::size_t transferSize_ = kDefaultDmaTransferSize;
    int lastWriteAttempts_ = 0;
};

}  // namespace g2710::rts8822
