// Dekorator koji belezi svaki transfer.
//
// Format je NAMERNO isti kao debug izlaz reference (hp3900_usb.c):
//
//   CTL DO: 40 04 e800 0000 0002
//   CTL DI: c0 04 e801 0100 0002
//   BLK DO: 512 bytes
//
// Zahvaljujuci tome golden fixture nije nas izmisljen format nego nesto sto se
// moze direktno uporediti sa `SANE_DEBUG_HP3900=255` izlazom, ako ikada
// zatreba poredjenje sa referentnom implementacijom u radu.
//
// Koristi se za golden sequence testove u G2710-2 i kao izvor .g2710trace
// fajlova koje ReplayTransport cita.

#pragma once

#include "ITransport.h"

#include <cstdint>
#include <string>
#include <vector>

namespace g2710 {

struct TraceEntry {
    enum class Kind {
        ControlIn,
        ControlOut,
        BulkRead,
        BulkWrite,
        WaitEvent,
        ResetPipe,
        SetTimeouts,
        PipeConfiguration,
        Cancel,
        Reopen,
    };

    Kind kind = Kind::ControlIn;
    std::uint16_t address = 0;
    Command command = Command::RegisterRead;
    std::vector<std::uint8_t> data;
    ErrorCode result = ErrorCode::Ok;
};

class TraceRecorder final : public ITransport {
public:
    explicit TraceRecorder(ITransport& inner) : inner_(inner) {}

    Status controlIn(std::uint16_t address, Command command,
                     std::span<std::byte> buffer) override;
    Status controlOut(std::uint16_t address, Command command,
                      std::span<const std::byte> buffer) override;
    std::size_t maxControlChunk() const noexcept override {
        return inner_.maxControlChunk();
    }

    Result<std::size_t> bulkRead(std::span<std::byte> buffer) override;
    Status bulkWrite(std::span<const std::byte> buffer) override;

    Result<std::uint32_t> waitEvent() override;

    Status resetPipe(PipeKind pipe) override;
    Status setTimeouts(const Timeouts& timeouts) override;
    Result<PipeConfiguration> pipeConfiguration() override;

    void cancel() noexcept override;
    Status reopen() override;
    bool isOpen() const noexcept override { return inner_.isOpen(); }
    const char* name() const noexcept override { return inner_.name(); }

    // --- zabelezeno ----------------------------------------------------
    const std::vector<TraceEntry>& entries() const noexcept { return entries_; }
    void clear() noexcept { entries_.clear(); }

    // Tekstualni oblik u formatu reference. `withData` dodaje red sa bajtovima
    // ispod svakog transfera; za poredjenje sekvenci obicno nije potreban.
    std::string format(bool withData = false) const;

private:
    ITransport& inner_;
    std::vector<TraceEntry> entries_;
};

}  // namespace g2710
