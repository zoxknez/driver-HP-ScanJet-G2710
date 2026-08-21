#include "ScanEngine.h"

#include "rts8822/Registers.h"
#include "rts8822/RegisterFile.h"
#include "rts8822/ScanRegisters.h"

#include <algorithm>
#include <cstring>

namespace g2710::sim {
namespace {

// Bank pocinje na 0xE800; sve adrese u Registers.h su apsolutne.
constexpr std::uint16_t kBankBase = 0xE800;

std::uint8_t byteAt(std::span<const std::uint8_t> bank, std::uint16_t address) noexcept {
    const std::size_t index = static_cast<std::size_t>(address - kBankBase);
    return index < bank.size() ? bank[index] : std::uint8_t{0};
}

int wordAt(std::span<const std::uint8_t> bank, std::uint16_t address) noexcept {
    return static_cast<int>(byteAt(bank, address)) |
           (static_cast<int>(byteAt(bank, static_cast<std::uint16_t>(address + 1))) << 8);
}

int fieldAt(std::span<const std::uint8_t> bank, std::uint16_t address,
            std::uint8_t mask) noexcept {
    return rts8822::bitsetGet(byteAt(bank, address), mask);
}

}  // namespace

void ScanEngine::setStepsPerLine(int steps) noexcept {
    stepsPerLine_ = std::max(1, steps);
}

bool ScanEngine::start(std::span<const std::uint8_t> bank, int motorPosition) {
    (void)motorPosition;
    reset();

    rts8822::CoordinateScaling scaling;
    scaling.resolutionRatio = fieldAt(bank, rts8822::reg::kResolutionRatio,
                                      rts8822::reg::kResolutionRatioMask);
    scaling.dummyLine = fieldAt(bank, rts8822::reg::kDummyLine, rts8822::reg::kDummyLineMask);
    if (!scaling.valid()) {
        // Cip bez ova dva polja ne bi umeo da protumaci sopstvene koordinate.
        return false;
    }

    rts8822::ScanGeometry inRegisters;
    inRegisters.left = wordAt(bank, rts8822::reg::kScanLeft);
    inRegisters.width = wordAt(bank, rts8822::reg::kScanRight) - inRegisters.left;

    const int topHigh = fieldAt(bank, rts8822::reg::kScanVerticalHigh,
                                rts8822::reg::kScanTopHighMask)
                        << 16;
    const int bottomHigh = fieldAt(bank, rts8822::reg::kScanVerticalHigh,
                                   rts8822::reg::kScanBottomHighMask)
                           << 16;
    inRegisters.top = wordAt(bank, rts8822::reg::kScanTop) | topHigh;
    inRegisters.height = (wordAt(bank, rts8822::reg::kScanBottom) | bottomHigh) - inRegisters.top;

    const rts8822::ScanGeometry pixels = rts8822::fromRegisterCoordinates(inRegisters, scaling);

    setup_.left = pixels.left;
    setup_.top = pixels.top;
    setup_.width = pixels.width;
    setup_.height = pixels.height;
    setup_.channelsPerDot =
        fieldAt(bank, rts8822::reg::kChannelsPerDot, rts8822::reg::kChannelsPerDotMask);
    setup_.depthCode = static_cast<image::DepthCode>(
        fieldAt(bank, rts8822::reg::kDepthCode, rts8822::reg::kDepthCodeMask));

    const std::uint8_t channelSize = byteAt(bank, rts8822::reg::kChannelSize);
    setup_.wideChannel = (channelSize & rts8822::reg::kChannelSizeWideBit) != 0 &&
                         (channelSize & rts8822::reg::kChannelSizeNarrowBit) == 0;

    setup_.lineOffsets.evenOdd =
        fieldAt(bank, rts8822::reg::kLineOffsetEvenOdd, rts8822::reg::kLineOffsetMask);
    setup_.lineOffsets.lineDistance =
        fieldAt(bank, rts8822::reg::kLineOffsetDistance, rts8822::reg::kLineOffsetMask);
    setup_.lineOffsets.lineDistancePlusEvenOdd =
        fieldAt(bank, rts8822::reg::kLineOffsetDistancePlus, rts8822::reg::kLineOffsetMask);
    setup_.lineOffsets.doubleLineDistance =
        fieldAt(bank, rts8822::reg::kLineOffsetDouble, rts8822::reg::kLineOffsetMask);
    setup_.lineOffsets.doublePlusEvenOdd =
        fieldAt(bank, rts8822::reg::kLineOffsetDoublePlus, rts8822::reg::kLineOffsetMask);

    rts8822::ScanFormat format;
    format.channelsPerDot = setup_.channelsPerDot;
    format.depthCode = setup_.depthCode;
    setup_.bytesPerLine = rts8822::bytesPerLine({0, 0, setup_.width, 1}, format);

    if (!setup_.valid() || setup_.bytesPerLine == 0) {
        setup_ = ScanSetup{};
        return false;
    }

    // Jedan red slike nosi `resolutionRatio` senzorskih redova, a senzorska
    // rezolucija je dvostruka motornoj - otud deljenje sa dva. Za 2400 dpi to
    // daje nulu, pa se podize na jedan: motor ne ume na pola koraka.
    setStepsPerLine(scaling.resolutionRatio / 2);

    line_.assign(setup_.bytesPerLine, 0);
    channel_.assign(static_cast<std::size_t>(setup_.width), 0);
    linePosition_ = line_.size();  // prvi read() renderuje red
    running_ = true;
    return true;
}

void ScanEngine::stop() noexcept { running_ = false; }

void ScanEngine::reset() noexcept {
    setup_ = ScanSetup{};
    running_ = false;
    linesDelivered_ = 0;
    stepsPerLine_ = 1;
    line_.clear();
    linePosition_ = 0;
    channel_.clear();
}

int ScanEngine::linesRemaining() const noexcept {
    if (!running_) {
        return 0;
    }
    return std::max(0, setup_.height - linesDelivered_);
}

void ScanEngine::renderLine(const VirtualCcd& ccd, const VirtualLamp& lamp, int motorPosition) {
    const std::size_t pixels = static_cast<std::size_t>(setup_.width);
    const bool lineart = setup_.depthCode == image::DepthCode::Lineart;
    const bool wide = setup_.wideChannel && !lineart;

    std::fill(line_.begin(), line_.end(), std::uint8_t{0});

    for (int channel = 0; channel < setup_.channelsPerDot; ++channel) {
        // Hardversko poravnanje: cip zadrzava kanal onoliko REDOVA koliko
        // kaze njegovo sestobitno polje, pa uzorak dolazi sa ranije pozicije.
        //
        // Polje koje se odseklo na nulu ovde daje nulu, i kanal ostaje
        // nepomeren. Tako se D3 vidi u slici umesto da se pretpostavlja.
        int heldLines = 0;
        if (channel == 1) {
            heldLines = setup_.lineOffsets.lineDistance;
        } else if (channel == 2) {
            heldLines = setup_.lineOffsets.doubleLineDistance;
        }

        const int samplePosition = motorPosition - heldLines * stepsPerLine_;
        const int scanResolution = ccd.geometry().motorResolution * stepsPerLine_ > 0
                                       ? ccd.geometry().motorResolution / stepsPerLine_
                                       : ccd.geometry().motorResolution;

        ccd.readLine(samplePosition, scanResolution, static_cast<CcdChannel>(channel), lamp,
                     std::span<std::uint16_t>(channel_.data(), pixels));

        for (std::size_t pixel = 0; pixel < pixels; ++pixel) {
            const std::uint16_t value = channel_[pixel];
            const std::size_t dot = pixel * static_cast<std::size_t>(setup_.channelsPerDot) +
                                    static_cast<std::size_t>(channel);

            if (lineart) {
                // Cip pakuje bit po tacki; simulator prati isti prag koji
                // referenca upisuje u registre.
                if (value >= 0x8000) {
                    const std::size_t byte = dot / 8;
                    if (byte < line_.size()) {
                        line_[byte] |= static_cast<std::uint8_t>(0x80u >> (dot % 8));
                    }
                }
            } else if (wide) {
                const std::size_t byte = dot * 2;
                if (byte + 1 < line_.size()) {
                    line_[byte] = static_cast<std::uint8_t>(value & 0xFF);
                    line_[byte + 1] = static_cast<std::uint8_t>(value >> 8);
                }
            } else {
                if (dot < line_.size()) {
                    line_[dot] = static_cast<std::uint8_t>(value >> 8);
                }
            }
        }
    }
}

std::size_t ScanEngine::read(std::span<std::byte> out, const VirtualCcd& ccd,
                             const VirtualLamp& lamp, VirtualMotor& motor) {
    if (!running_ || out.empty()) {
        return 0;
    }

    std::size_t written = 0;
    while (written < out.size()) {
        if (linePosition_ >= line_.size()) {
            if (linesDelivered_ >= setup_.height) {
                // Slika je gotova; cip spusta bit izvrsavanja.
                running_ = false;
                break;
            }
            renderLine(ccd, lamp, motor.position());
            linePosition_ = 0;
            ++linesDelivered_;

            // Motor se pomera JEDNOM po redu, kao pri stvarnom skeniranju.
            // Ishod se ne ignorise: udarac u kraj staze zaustavlja isporuku,
            // jer bi i na hardveru dalji redovi bili isti red ponovljen.
            motor.setDirection(MotorDirection::Forward);
            if (motor.step(stepsPerLine_) == MotorOutcome::HitFarLimit) {
                running_ = false;
            }
        }

        const std::size_t chunk =
            std::min(out.size() - written, line_.size() - linePosition_);
        std::memcpy(out.data() + written, line_.data() + linePosition_, chunk);
        linePosition_ += chunk;
        written += chunk;
    }
    return written;
}

}  // namespace g2710::sim
