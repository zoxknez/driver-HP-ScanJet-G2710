#include "SimTransport.h"

#include "G2710Profile.generated.h"

#include <algorithm>
#include <cstring>

namespace g2710::sim {
namespace {

constexpr std::size_t kEepromSize = 512;

}  // namespace

SimTransport::SimTransport()
    : registers_(static_cast<std::size_t>(profile::kRegisterBankLength), 0),
      eeprom_(kEepromSize, 0) {
    // Chipset id koji Chipset_Detect ocekuje; tacna adresa i vrednost se
    // zakljucavaju u G2710-2 kada se registarska mapa imenuje.
    registers_[0] = static_cast<std::uint8_t>(profile::kChipsetModelId);

    // DMA je spreman odmah. Pravi cip nije, i to je razlog sto postoji
    // RTS_DMA_WaitReady - modeliranje kasnjenja dolazi u G2710-3.
    registers_[0x709] = 0x01;  // kDmaStatus, kDmaStatusReadyBit
}

std::size_t SimTransport::registerIndex(std::uint16_t address) const noexcept {
    const auto base = static_cast<std::size_t>(profile::kRegisterBankBase);
    const auto addr = static_cast<std::size_t>(address);
    return addr >= base ? addr - base : addr;
}

Status SimTransport::checkOpen(const char* context) const {
    if (!open_) {
        return fail(ErrorCode::NotOpen, context);
    }
    if (cancelled_) {
        return fail(ErrorCode::Cancelled, context);
    }
    return ok();
}

Status SimTransport::controlIn(std::uint16_t address, Command command,
                               std::span<std::byte> buffer) {
    if (const Status s = checkOpen("sim: controlIn"); !s) {
        return s;
    }
    ++controlIns_;

    switch (command) {
        case Command::RegisterRead: {
            const std::size_t start = registerIndex(address);
            for (std::size_t i = 0; i < buffer.size(); ++i) {
                const std::size_t index = start + i;
                buffer[i] = static_cast<std::byte>(
                    index < registers_.size() ? registers_[index] : 0);
            }
            return ok();
        }

        case Command::Eeprom: {
            for (std::size_t i = 0; i < buffer.size(); ++i) {
                const std::size_t index = address + i;
                buffer[i] = static_cast<std::byte>(
                    index < eeprom_.size() ? eeprom_[index] : 0);
            }
            return ok();
        }

        default:
            // Referenca nikada ne cita kroz ostale komande.
            return fail(ErrorCode::InvalidArgument, "sim: controlIn nad komandom koja nije citljiva");
    }
}

Status SimTransport::controlOut(std::uint16_t address, Command command,
                                std::span<const std::byte> buffer) {
    if (const Status s = checkOpen("sim: controlOut"); !s) {
        return s;
    }
    ++controlOuts_;

    switch (command) {
        case Command::RegisterWrite: {
            const std::size_t start = registerIndex(address);
            for (std::size_t i = 0; i < buffer.size(); ++i) {
                const std::size_t index = start + i;
                if (index < registers_.size()) {
                    registers_[index] = static_cast<std::uint8_t>(buffer[i]);
                }
            }
            return ok();
        }

        case Command::Eeprom: {
            for (std::size_t i = 0; i < buffer.size(); ++i) {
                const std::size_t index = address + i;
                if (index < eeprom_.size()) {
                    eeprom_[index] = static_cast<std::uint8_t>(buffer[i]);
                }
            }
            return ok();
        }

        case Command::ChipsetReset:
            ++chipsetResets_;
            std::fill(registers_.begin(), registers_.end(), std::uint8_t{0});
            registers_[0] = static_cast<std::uint8_t>(profile::kChipsetModelId);
            return ok();

        case Command::DmaEnableRead:
        case Command::DmaEnableWrite: {
            // 6 bajtova: options MSB-first [0..2], velicina u RECIMA LSB-first [3..5].
            if (buffer.size() != 6) {
                return fail(ErrorCode::InvalidArgument, "sim: DMA enable ocekuje 6 bajtova");
            }
            const auto words = static_cast<std::size_t>(buffer[3]) |
                               (static_cast<std::size_t>(buffer[4]) << 8) |
                               (static_cast<std::size_t>(buffer[5]) << 16);
            dmaLength_ = words * 2;
            // Memorija se cuva, ne brise: read-back posle upisa mora vracati
            // ono sto je upisano, inace verifikaciona petlja nema smisla.
            if (dmaMemory_.size() < dmaLength_) {
                dmaMemory_.resize(dmaLength_, 0);
            }
            return ok();
        }

        case Command::DmaCancel:
            dmaLength_ = 0;
            return ok();

        case Command::DmaOpType: {
            if (buffer.size() != 2) {
                return fail(ErrorCode::InvalidArgument, "sim: DMA op type ocekuje 2 bajta");
            }
            dmaOperationType_ = static_cast<std::uint16_t>(
                static_cast<std::uint16_t>(buffer[0]) |
                (static_cast<std::uint16_t>(buffer[1]) << 8));
            return ok();
        }

        default:
            return fail(ErrorCode::InvalidArgument, "sim: nepodrzana komanda za upis");
    }
}

Result<std::size_t> SimTransport::bulkRead(std::span<std::byte> buffer) {
    if (const Status s = checkOpen("sim: bulkRead"); !s) {
        return s.error();
    }
    const std::size_t count = (std::min)(buffer.size(), dmaMemory_.size());
    for (std::size_t i = 0; i < count; ++i) {
        buffer[i] = static_cast<std::byte>(dmaMemory_[i]);
    }

    // Injektovana greska: pokvari prvi bajt da bi verifikaciona petlja morala
    // da ponovi upis.
    if (corruptReadBacks_ > 0 && count > 0) {
        --corruptReadBacks_;
        buffer[0] = static_cast<std::byte>(static_cast<std::uint8_t>(buffer[0]) ^ 0xFF);
    }
    return count;
}

Status SimTransport::bulkWrite(std::span<const std::byte> buffer) {
    if (const Status s = checkOpen("sim: bulkWrite"); !s) {
        return s;
    }
    if (dmaMemory_.size() < buffer.size()) {
        dmaMemory_.resize(buffer.size(), 0);
    }
    for (std::size_t i = 0; i < buffer.size(); ++i) {
        dmaMemory_[i] = static_cast<std::uint8_t>(buffer[i]);
    }
    return ok();
}

Result<std::uint32_t> SimTransport::waitEvent() {
    if (const Status s = checkOpen("sim: waitEvent"); !s) {
        return s.error();
    }
    if (pendingEvent_ == 0) {
        return fail(ErrorCode::Timeout, "sim: nema dogadjaja");
    }
    const std::uint32_t mask = pendingEvent_;
    pendingEvent_ = 0;
    return mask;
}

Status SimTransport::resetPipe(PipeKind) {
    return checkOpen("sim: resetPipe");
}

Status SimTransport::setTimeouts(const Timeouts& timeouts) {
    timeouts_ = timeouts;
    return ok();
}

Result<PipeConfiguration> SimTransport::pipeConfiguration() {
    PipeConfiguration config;
    config.bulkIn = profile::kBulkInEndpoint;
    config.bulkOut = profile::kBulkOutEndpoint;
    config.interrupt = 0x83;
    config.hasInterrupt = true;
    return config;
}

void SimTransport::cancel() noexcept {
    cancelled_ = true;
}

Status SimTransport::reopen() {
    open_ = true;
    cancelled_ = false;
    return ok();
}

std::uint8_t SimTransport::peekRegister(std::uint16_t address) const noexcept {
    const std::size_t index = registerIndex(address);
    return index < registers_.size() ? registers_[index] : 0;
}

void SimTransport::pokeRegister(std::uint16_t address, std::uint8_t value) noexcept {
    const std::size_t index = registerIndex(address);
    if (index < registers_.size()) {
        registers_[index] = value;
    }
}

Result<std::unique_ptr<ITransport>> SimTransportProvider::create(const DeviceRef&) {
    return std::unique_ptr<ITransport>(std::make_unique<SimTransport>());
}

}  // namespace g2710::sim
