#include "SimTransport.h"

#include "G2710Profile.generated.h"
#include "rts8822/Registers.h"

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

    identity_.vendorId = profile::kUsbVendorId;
    identity_.productId = profile::kUsbProductId;
    identity_.bcdDevice = 0x0100;
}

Result<DeviceIdentity> SimTransport::identity() {
    if (const Status s = checkOpen("sim: identity"); !s) {
        return s.error();
    }
    return identity_;
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

void SimTransport::advanceTime(std::uint32_t milliseconds) noexcept {
    flatbedLamp_.advance(milliseconds);
    tmaLamp_.advance(milliseconds);
}

void SimTransport::mirrorHardwareIntoRegisters() noexcept {
    namespace reg = g2710::rts8822::reg;

    // Home senzor. Engine ovo cita preko Head_IsAtHome; simulacija ne sme
    // imati poseban kanal koji u produkciji ne postoji.
    auto& headByte = registers_[registerIndex(reg::kHeadSensor)];
    headByte = static_cast<std::uint8_t>(
        motor_.isAtHome() ? (headByte | reg::kHeadAtHomeBit)
                          : (headByte & static_cast<std::uint8_t>(~reg::kHeadAtHomeBit)));

    // Status lampi.
    auto& lampByte = registers_[registerIndex(reg::kLampStatus)];
    lampByte = static_cast<std::uint8_t>(
        flatbedLamp_.isOn() ? (lampByte | reg::kLampStatusFlbBit)
                            : (lampByte & static_cast<std::uint8_t>(~reg::kLampStatusFlbBit)));
    lampByte = static_cast<std::uint8_t>(
        tmaLamp_.isOn() ? (lampByte | reg::kLampStatusTmaBit)
                        : (lampByte & static_cast<std::uint8_t>(~reg::kLampStatusTmaBit)));

    // kLampMode (Regs[0x154]) se NAMERNO ne dira.
    //
    // Lamp_Status_Get u BL-03A grani testira bas taj bajt za TMA, ali ga
    // NIJEDNA funkcija u referenci ne upisuje - Lamp_Status_Set pise
    // Regs[0x155]. To je defekt D2 iz docs/REFERENCE-DEFECTS.md.
    //
    // Simulator koji bi ovde sintetisao bit ucinio bi Get i Set prividno
    // saglasnim i sakrio otvoreno pitanje koje H3 mora da razresi. Zato se
    // defekt REPRODUKUJE: posle setLamp(Tma, true), lampStatus() prijavljuje
    // TMA kao ugasenu, tacno kao sto bi se ponasala referenca.

    // PWM duty cycle koji je engine postavio vraca se kao stvarno stanje.
    auto& pwmByte = registers_[registerIndex(reg::kLampPwm)];
    flatbedLamp_.setDutyCycle(static_cast<std::uint8_t>(pwmByte & reg::kLampPwmDutyMask));
}

void SimTransport::applyRegisterWritesToHardware() noexcept {
    namespace reg = g2710::rts8822::reg;

    // Bitovi u kLampStatus PALE lampu; preslikavanje ih posle samo potvrdjuje.
    const std::uint8_t lampByte = registers_[registerIndex(reg::kLampStatus)];

    if ((lampByte & reg::kLampStatusFlbBit) != 0) {
        flatbedLamp_.turnOn();
    } else {
        flatbedLamp_.turnOff();
    }

    if ((lampByte & reg::kLampStatusTmaBit) != 0) {
        tmaLamp_.turnOn();
    } else {
        tmaLamp_.turnOff();
    }
}

Status SimTransport::applyFault(TransferKind kind, const char* context) {
    if (const auto error = faults_.nextFault(kind)) {
        return fail(*error, context);
    }
    return ok();
}

Status SimTransport::controlIn(std::uint16_t address, Command command,
                               std::span<std::byte> buffer) {
    if (const Status s = checkOpen("sim: controlIn"); !s) {
        return s;
    }
    if (const Status s = applyFault(TransferKind::ControlIn, "sim: controlIn"); !s) {
        return s;
    }
    ++controlIns_;
    mirrorHardwareIntoRegisters();

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
    if (const Status s = applyFault(TransferKind::ControlOut, "sim: controlOut"); !s) {
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
            applyRegisterWritesToHardware();
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
    if (const Status s = applyFault(TransferKind::BulkRead, "sim: bulkRead"); !s) {
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
    if (const Status s = applyFault(TransferKind::BulkWrite, "sim: bulkWrite"); !s) {
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
    if (const Status s = applyFault(TransferKind::Event, "sim: waitEvent"); !s) {
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

bool SimTransport::isHardwareBackedRegister(std::uint16_t address) noexcept {
    namespace reg = g2710::rts8822::reg;
    return address == reg::kHeadSensor || address == reg::kLampStatus ||
           address == reg::kLampMode;
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
