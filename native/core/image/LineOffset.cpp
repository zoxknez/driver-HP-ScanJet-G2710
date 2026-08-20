#include "LineOffset.h"

#include <algorithm>

namespace g2710::image {

bool LineOffsetRegisters::fitsInHardware() const noexcept {
    return largest() <= kHardwareOffsetMax;
}

int LineOffsetRegisters::largest() const noexcept {
    return std::max({evenOdd, lineDistance, lineDistancePlusEvenOdd,
                     doubleLineDistance, doublePlusEvenOdd});
}

LineOffsetRegisters computeLineOffsets(int sensorLineDistance,
                                       int sensorEvenOddDistance,
                                       int sensorResolution, int scanResolution,
                                       bool highResolution) noexcept {
    LineOffsetRegisters registers;
    if (sensorResolution <= 0) {
        return registers;
    }

    // rts8822.c:8690 - celobrojno deljenje, kao u referenci.
    const int lineDistance = (sensorLineDistance * scanResolution) / sensorResolution;

    // rts8822.c:8694 - ispod visoke rezolucije even/odd razmak se NE koristi.
    const int evenOdd = highResolution
                            ? (sensorEvenOddDistance * scanResolution) / sensorResolution
                            : 0;

    registers.evenOdd = evenOdd;
    registers.lineDistance = lineDistance;
    registers.lineDistancePlusEvenOdd = lineDistance + evenOdd;
    registers.doubleLineDistance = lineDistance * 2;
    registers.doublePlusEvenOdd = lineDistance * 2 + evenOdd;
    return registers;
}

bool hardwareAlignmentSupported(int sensorLineDistance, int sensorEvenOddDistance,
                                int sensorResolution, int scanResolution,
                                bool highResolution) noexcept {
    return computeLineOffsets(sensorLineDistance, sensorEvenOddDistance,
                              sensorResolution, scanResolution, highResolution)
        .fitsInHardware();
}

// --- softversko poravnanje ---------------------------------------------------

LineOffsetCorrector::LineOffsetCorrector(std::size_t pixelsPerLine,
                                         int lineDistanceInLines)
    : pixels_(pixelsPerLine), distance_(std::max(0, lineDistanceInLines)) {}

void LineOffsetCorrector::reset() {
    for (auto& queue : queues_) {
        queue.clear();
    }
    produced_ = 0;
}

Status LineOffsetCorrector::push(std::size_t channel,
                                 std::span<const std::uint16_t> line) {
    if (channel >= kChannels) {
        return fail(ErrorCode::InvalidArgument, "lineOffset: nepostojeci kanal");
    }
    if (line.size() != pixels_) {
        return fail(ErrorCode::InvalidArgument, "lineOffset: pogresna duzina reda");
    }

    queues_[channel].emplace_back(line.begin(), line.end());
    return ok();
}

bool LineOffsetCorrector::hasOutput() const noexcept {
    // Crveni red mora biti stariji za dva razmaka od plavog, a zeleni za jedan.
    // Izlaz je spreman tek kada svaki kanal ima svoj odgovarajuci red.
    const auto red = static_cast<int>(queues_[0].size());
    const auto green = static_cast<int>(queues_[1].size());
    const auto blue = static_cast<int>(queues_[2].size());

    return red > produced_ + distance_ * 2 && green > produced_ + distance_ &&
           blue > produced_;
}

Status LineOffsetCorrector::pop(std::span<std::uint16_t> out) {
    if (!hasOutput()) {
        return fail(ErrorCode::InvalidState, "lineOffset: nema spremnog reda");
    }
    if (out.size() != kChannels * pixels_) {
        return fail(ErrorCode::InvalidArgument, "lineOffset: pogresna duzina izlaza");
    }

    // Svaki kanal se uzima sa svog kasnjenja, cime se tri pomerene ravni
    // poklapaju na istom redu mete.
    const std::array<int, kChannels> delay{distance_ * 2, distance_, 0};

    for (std::size_t channel = 0; channel < kChannels; ++channel) {
        const auto index = static_cast<std::size_t>(produced_ + delay[channel]);
        const auto& source = queues_[channel][index];
        std::copy(source.begin(), source.end(), out.begin() + channel * pixels_);
    }

    ++produced_;
    return ok();
}

}  // namespace g2710::image
