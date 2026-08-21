#include "AdcCalibration.h"

#include <algorithm>
#include <cmath>

namespace g2710::calib {
namespace {

// rts8822.c:11666. 0.00390625 je 1/256, 0.909090... je 40/44.
constexpr double kInverse256 = 0.00390625;
constexpr double kGainThreshold = 0.9090909090909091;

// Granice VGAG polja.
constexpr int kMaxGain = 31;

// Devetobitni DCG.
constexpr int kDcgBreak = 0x100;
constexpr int kDcgMax = 0x1FF;

// rts8822.c:11855 - vrh mora nadmasiti cilj za bar toliko.
constexpr int kPeakMargin = 5;

}  // namespace

const char* toString(GainArithmetic arithmetic) noexcept {
    switch (arithmetic) {
        case GainArithmetic::Reference: return "reference";
        case GainArithmetic::Corrected: return "corrected";
    }
    return "?";
}

OffsetWindow offsetWindow(int resolution, CalibrationSection section) noexcept {
    for (const auto& row : profile::kOffsets) {
        if (row.resolution != resolution) {
            continue;
        }
        const profile::OffsetPair& pair =
            section == CalibrationSection::Reflective    ? row.reflective
            : section == CalibrationSection::Transparent ? row.transparent
                                                         : row.negative;
        return OffsetWindow{pair.left, pair.width};
    }
    return OffsetWindow{};
}

bool GainOffsetState::allReady() const noexcept {
    for (std::size_t channel = 0; channel < kChannels; ++channel) {
        if (!evenReady[channel] || !oddReady[channel]) {
            return false;
        }
    }
    return true;
}

GainOffsetState initialGainOffset(int usbSpeed) noexcept {
    GainOffsetState state;
    for (const auto& row : profile::kGainOffsets) {
        if (row.usb != usbSpeed) {
            continue;
        }
        for (std::size_t channel = 0; channel < kChannels; ++channel) {
            state.evenDcg[channel] = row.values.evenOffset1[channel];
            state.oddDcg[channel] = row.values.oddOffset1[channel];
            state.gain[channel] = row.values.vgag1[channel];
        }
        break;
    }
    return state;
}

int decodeDcg(int registerValue) noexcept {
    // rts8822.c:12374 - `d = mydcg[channel]; if (d < 0x100) d = 0xff - d;`
    return registerValue < kDcgBreak ? 0xFF - registerValue : registerValue;
}

int encodeDcg(int signedValue) noexcept {
    // rts8822.c:12407 - `mydcg[channel] = (d < 0x100) ? 0x100 - d : d;`
    //
    // Konstanta se razlikuje od one u decodeDcg (0x100 naspram 0xFF), pa
    // encode(decode(x)) NIJE x. Preneto doslovno - vidi D6.
    return signedValue < kDcgBreak ? kDcgBreak - signedValue : signedValue;
}

int offsetTargetFor(const CalibrationConfig& config, std::size_t channel) noexcept {
    if (channel >= kChannels) {
        return 0x80;
    }
    // rts8822.c:12213 - pomak za osam bita, a nula postaje 0x80.
    const int target = config.offsetAvgTarget[channel] << 8;
    return target == 0 ? 0x80 : target;
}

Result<AdcGainResult> computeAdcGain(const CalibrationConfig& config,
                                     std::span<const double> peakAverage,
                                     const GainOffsetState& state, int depth,
                                     GainArithmetic arithmetic) {
    if (peakAverage.size() != kChannels) {
        return fail(ErrorCode::InvalidArgument, "computeAdcGain: ocekuje se tri kanala");
    }
    if (depth <= 0 || depth > 16) {
        return fail(ErrorCode::InvalidArgument, "computeAdcGain: neispravna dubina");
    }

    AdcGainResult result;
    const double fullScale = static_cast<double>(1 << depth);

    for (std::size_t channel = 0; channel < kChannels; ++channel) {
        if (peakAverage[channel] <= 0.0) {
            // Kanal bez signala. Referenca bi ovde delila nulom; mi odbijamo,
            // jer nula znaci da lampa ne gori ili da traka nije ispod glave.
            return fail(ErrorCode::DeviceError,
                        "computeAdcGain: kanal nema signala - lampa ili traka?");
        }

        const int currentGain = state.gain[channel];

        // rts8822.c:11670. Ovo je D5: referenca deli CELOBROJNO.
        const double factor = arithmetic == GainArithmetic::Reference
                                  ? static_cast<double>((44 - currentGain) / 40)
                                  : (44.0 - currentGain) / 40.0;
        result.appliedFactor[channel] = factor;

        double value = (((config.whiteReference[channel] * fullScale) *
                         config.gainTargetFactor) *
                        kInverse256 / peakAverage[channel]) *
                       factor;

        if (value > kGainThreshold) {
            value = std::min(44.0 - (40.0 / value), static_cast<double>(kMaxGain));
            result.gain[channel] = static_cast<int>(value) & 0xFF;
        } else {
            result.gain[channel] = 0;
        }
    }

    // rts8822.c:11855 - dovoljan je JEDAN kanal iznad praga.
    for (std::size_t channel = 0; channel < kChannels; ++channel) {
        if (peakAverage[channel] >= config.offsetAvgTarget[channel] + kPeakMargin) {
            result.reachedTarget = true;
            break;
        }
    }
    return result;
}

Result<AdcOffsetStep> advanceAdcOffset(const CalibrationConfig& config, OffsetParity parity,
                                       std::span<const double> channelSum, int samplesUsed,
                                       GainOffsetState* state) {
    if (state == nullptr) {
        return fail(ErrorCode::InvalidArgument, "advanceAdcOffset: nema stanja");
    }
    if (channelSum.size() != kChannels) {
        return fail(ErrorCode::InvalidArgument, "advanceAdcOffset: ocekuje se tri kanala");
    }
    if (samplesUsed <= 0) {
        return fail(ErrorCode::InvalidArgument, "advanceAdcOffset: prazan prozor");
    }

    auto& dcg = parity == OffsetParity::Even ? state->evenDcg : state->oddDcg;
    auto& ready = parity == OffsetParity::Even ? state->evenReady : state->oddReady;

    AdcOffsetStep step;

    for (std::size_t channel = 0; channel < kChannels; ++channel) {
        if (ready[channel]) {
            continue;
        }

        // rts8822.c:12358 - zbir se pomera za osam bita PA deli brojem uzoraka.
        const long long scaled = static_cast<long long>(channelSum[channel]) << 8;

        if (scaled == 0) {
            // Potpuno crno: gurni offset na maksimum, pa probaj ponovo.
            if (dcg[channel] != kDcgMax) {
                dcg[channel] = kDcgMax;
                step.changed = true;
            } else {
                ready[channel] = true;
                ++step.settled;
            }
            continue;
        }

        const long long average = scaled / samplesUsed;
        const long long target = offsetTargetFor(config, channel);

        // rts8822.c:12366 - smer, pa apsolutna razlika.
        const bool belowTarget = average < target;
        const long long difference = belowTarget ? target - average : average - target;

        int decoded = decodeDcg(dcg[channel]);

        if (belowTarget) {
            if (decoded + difference > kDcgMax) {
                // Offset ne staje: jedini preostali potez je manje pojacanje.
                if (state->gain[channel] > 0) {
                    --state->gain[channel];
                    ++step.gainReductions;
                    step.changed = true;
                } else {
                    ready[channel] = true;
                    ++step.settled;
                }
                continue;
            }
            decoded += static_cast<int>(difference);
        } else {
            if (difference > decoded) {
                if (state->gain[channel] > 0) {
                    --state->gain[channel];
                    ++step.gainReductions;
                    step.changed = true;
                } else {
                    ready[channel] = true;
                    ++step.settled;
                }
                continue;
            }
            decoded -= static_cast<int>(difference);
        }

        const int encoded = encodeDcg(decoded);
        if (encoded != dcg[channel]) {
            dcg[channel] = encoded;
            step.changed = true;
        } else {
            ready[channel] = true;
            ++step.settled;
        }
    }

    return step;
}

}  // namespace g2710::calib
