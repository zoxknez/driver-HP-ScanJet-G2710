#include "Dma.h"

#include "Registers.h"

#include <algorithm>
#include <array>
#include <thread>
#include <vector>

namespace g2710::rts8822 {
namespace {

// rts8822.c:4245 data_msb_set(&buffer[0], options, 3)
//                data_lsb_set(&buffer[3], size / 2, 3)
//
// Prva tri bajta su `options` MSB-first, druga tri broj RECI (size / 2)
// LSB-first. Zamena reci i bajtova ovde daje pola slike, pa je vredna
// posebnog testa.
std::array<std::byte, 6> enableBlock(std::uint32_t options, std::size_t sizeBytes) {
    const std::size_t words = sizeBytes / 2;
    return {
        static_cast<std::byte>((options >> 16) & 0xFF),
        static_cast<std::byte>((options >> 8) & 0xFF),
        static_cast<std::byte>(options & 0xFF),
        static_cast<std::byte>(words & 0xFF),
        static_cast<std::byte>((words >> 8) & 0xFF),
        static_cast<std::byte>((words >> 16) & 0xFF),
    };
}

}  // namespace

void Dma::setTransferSize(std::size_t bytes) noexcept {
    transferSize_ = bytes == 0 ? kDefaultDmaTransferSize : bytes;
}

Status Dma::setOperationType(std::uint16_t operationType) {
    if (const Status allowed = gate_.require(SafetyLevel::Lamp, "dma.setOperationType");
        !allowed) {
        return allowed;
    }
    const std::array<std::byte, 2> payload{
        static_cast<std::byte>(operationType & 0xFF),
        static_cast<std::byte>((operationType >> 8) & 0xFF),
    };
    return registers_.transport().controlOut(0x0000, Command::DmaOpType, payload);
}

Status Dma::reset() {
    return setOperationType(0x0000);
}

Status Dma::cancel() {
    if (const Status allowed = gate_.require(SafetyLevel::Lamp, "dma.cancel"); !allowed) {
        return allowed;
    }
    const std::array<std::byte, 2> payload{std::byte{0}, std::byte{0}};
    return registers_.transport().controlOut(0x0000, Command::DmaCancel, payload);
}

Status Dma::enable(Command command, std::uint16_t dmacs, std::uint32_t options,
                   std::size_t size) {
    const auto block = enableBlock(options, size);
    return registers_.transport().controlOut(dmacs, command, block);
}

Status Dma::enableRead(std::uint16_t dmacs, std::uint32_t options, std::size_t size) {
    return enable(Command::DmaEnableRead, dmacs, options, size);
}

Status Dma::enableWrite(std::uint16_t dmacs, std::uint32_t options, std::size_t size) {
    return enable(Command::DmaEnableWrite, dmacs, options, size);
}

Status Dma::waitReady(const PollPolicy& policy) {
    if (const Status allowed = gate_.require(SafetyLevel::ReadOnly, "dma.waitReady");
        !allowed) {
        return allowed;
    }

    const auto deadline = std::chrono::steady_clock::now() + policy.timeout;
    for (;;) {
        auto value = registers_.readByte(reg::kDmaStatus);
        if (!value) {
            return value.error();
        }
        if ((value.value() & reg::kDmaStatusReadyBit) != 0) {
            return ok();
        }
        if (std::chrono::steady_clock::now() >= deadline) {
            return fail(ErrorCode::Timeout, "dma.waitReady");
        }
        if (policy.interval.count() > 0) {
            std::this_thread::sleep_for(policy.interval);
        }
    }
}

Status Dma::bulkReadAll(std::span<std::byte> buffer) {
    std::size_t position = 0;
    while (position < buffer.size()) {
        const std::size_t chunk = (std::min)(transferSize_, buffer.size() - position);
        auto read = registers_.transport().bulkRead(buffer.subspan(position, chunk));
        if (!read) {
            return read.error();
        }
        if (read.value() == 0) {
            return fail(ErrorCode::ShortTransfer, "dma.bulkRead");
        }
        position += read.value();
    }
    return ok();
}

Status Dma::bulkWriteAll(std::span<const std::byte> buffer) {
    std::size_t position = 0;
    while (position < buffer.size()) {
        const std::size_t chunk = (std::min)(transferSize_, buffer.size() - position);
        if (const Status s = registers_.transport().bulkWrite(buffer.subspan(position, chunk));
            !s) {
            return s;
        }
        position += chunk;
    }
    return ok();
}

Status Dma::read(std::uint16_t dmacs, std::uint32_t options, std::span<std::byte> buffer) {
    if (const Status allowed = gate_.require(SafetyLevel::Acquire, "dma.read"); !allowed) {
        return allowed;
    }
    if (buffer.empty()) {
        return fail(ErrorCode::InvalidArgument, "dma.read: prazan bafer");
    }

    if (const Status s = reset(); !s) {
        return s;
    }
    if (const Status s = enableRead(dmacs, options, buffer.size()); !s) {
        return s;
    }
    return bulkReadAll(buffer);
}

Status Dma::write(std::uint16_t dmacs, std::uint32_t options,
                  std::span<const std::byte> buffer) {
    if (const Status allowed = gate_.require(SafetyLevel::Acquire, "dma.write"); !allowed) {
        return allowed;
    }
    if (buffer.empty()) {
        return fail(ErrorCode::InvalidArgument, "dma.write: prazan bafer");
    }

    lastWriteAttempts_ = 0;

    if (const Status s = reset(); !s) {
        return s;
    }
    if (const Status s = enableWrite(dmacs, options, buffer.size()); !s) {
        return s;
    }

    std::vector<std::byte> readBack(buffer.size());

    for (int attempt = 1; attempt <= kDmaWriteAttempts; ++attempt) {
        lastWriteAttempts_ = attempt;

        if (const Status s = bulkWriteAll(buffer); !s) {
            return s;
        }

        // Verifikacija: prebaci DMA u citanje i procitaj isti opseg nazad.
        if (const Status s = enableRead(dmacs, options, buffer.size()); !s) {
            return s;
        }
        if (const Status s = bulkReadAll(readBack); !s) {
            return s;
        }

        if (std::equal(buffer.begin(), buffer.end(), readBack.begin())) {
            return ok();
        }

        // Razlika: otkazi, ponovo pripremi upis i probaj opet.
        if (const Status s = cancel(); !s) {
            return s;
        }
        if (const Status s = enableWrite(dmacs, options, buffer.size()); !s) {
            return s;
        }
    }

    return fail(ErrorCode::DeviceError, "dma.write: verifikacija nije uspela ni posle 10 pokusaja");
}

}  // namespace g2710::rts8822
