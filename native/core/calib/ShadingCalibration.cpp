#include "ShadingCalibration.h"

#include <algorithm>
#include <cmath>

namespace g2710::calib {
namespace {

constexpr double kFullScale = 65535.0;

// Usredni `lines` redova datog kanala u `out`.
Status averageLines(const LineReader& reader, ShadingStrip strip, std::size_t channel,
                    int lines, std::size_t pixels, std::vector<double>& out) {
    out.assign(pixels, 0.0);
    if (lines <= 0) {
        return fail(ErrorCode::InvalidArgument, "shading: broj redova mora biti pozitivan");
    }

    std::vector<std::uint16_t> line(pixels);
    for (int i = 0; i < lines; ++i) {
        if (const Status s = reader(strip, channel, i, line); !s) {
            return s;
        }
        for (std::size_t p = 0; p < pixels; ++p) {
            out[p] += line[p];
        }
    }

    const double divisor = static_cast<double>(lines);
    for (double& value : out) {
        value /= divisor;
    }
    return ok();
}

double medianOf(std::vector<double> values) {
    if (values.empty()) {
        return 0.0;
    }
    const std::size_t middle = values.size() / 2;
    std::nth_element(values.begin(), values.begin() + static_cast<std::ptrdiff_t>(middle),
                     values.end());
    return values[middle];
}

}  // namespace

const char* toString(ShadingStrip strip) noexcept {
    switch (strip) {
        case ShadingStrip::Black: return "crna";
        case ShadingStrip::White: return "bela";
    }
    return "?";
}

const char* toString(ShadingRejection reason) noexcept {
    switch (reason) {
        case ShadingRejection::None:             return "prihvaceno";
        case ShadingRejection::NoDynamicRange:   return "bela i crna se ne razlikuju";
        case ShadingRejection::TooManyOutliers:  return "previse piksela van granica";
        case ShadingRejection::EmptyMeasurement: return "prazno merenje";
    }
    return "?";
}

double ShadingCalibration::scaledWhiteTarget(const CalibrationConfig& config,
                                             std::size_t channel,
                                             int sensorBitDepth) noexcept {
    const auto reference = static_cast<double>(config.whiteReference[channel]);
    const int referenceBits = config.referenceBitDepth > 0 ? config.referenceBitDepth : 8;

    if (sensorBitDepth <= referenceBits) {
        return reference;
    }

    // Puna skala na obe dubine; odnos cuva relativnu vrednost reference.
    const double referenceFull = std::pow(2.0, referenceBits) - 1.0;
    const double sensorFull = std::pow(2.0, sensorBitDepth) - 1.0;
    return reference * sensorFull / referenceFull;
}

Result<ShadingCoefficients> ShadingCalibration::measure(const CalibrationConfig& config,
                                                        std::size_t pixelsPerLine,
                                                        const LineReader& reader,
                                                        int sensorBitDepth,
                                                        ShadingMeasurement* stats) {
    if (pixelsPerLine == 0) {
        return fail(ErrorCode::InvalidArgument, "shading: prazan red");
    }
    if (!reader) {
        return fail(ErrorCode::InvalidArgument, "shading: nedostaje citac redova");
    }

    ShadingCoefficients coefficients;
    coefficients.pixelsPerLine = pixelsPerLine;
    coefficients.darkOffset.assign(kChannels * pixelsPerLine, 0.0);
    coefficients.gain.assign(kChannels * pixelsPerLine, 1.0);

    const int darkLines = std::max(1, config.blackShadingHeight);
    const int whiteLines = std::max(1, config.whiteShadingHeight);

    std::vector<double> dark;
    std::vector<double> white;

    for (std::size_t channel = 0; channel < kChannels; ++channel) {
        // Crna traka prva: bez nje se odziv bele ne moze razdvojiti od tamne
        // struje.
        if (const Status s = averageLines(reader, ShadingStrip::Black, channel, darkLines,
                                         pixelsPerLine, dark);
            !s) {
            return s.error();
        }
        if (const Status s = averageLines(reader, ShadingStrip::White, channel, whiteLines,
                                         pixelsPerLine, white);
            !s) {
            return s.error();
        }

        const double target = scaledWhiteTarget(config, channel, sensorBitDepth);

        for (std::size_t pixel = 0; pixel < pixelsPerLine; ++pixel) {
            const std::size_t index = channel * pixelsPerLine + pixel;

            coefficients.darkOffset[index] = dark[pixel];

            const double response = white[pixel] - dark[pixel];
            // Piksel bez odziva ostaje na pojacanju 1.0; validator ce ga
            // prijaviti kao outlier umesto da ovde nastane beskonacnost.
            coefficients.gain[index] = response > 0.0 ? (target / response) : 0.0;
        }
    }

    if (stats != nullptr) {
        stats->darkLines = darkLines;
        stats->whiteLines = whiteLines;
    }
    return coefficients;
}

ShadingValidation ShadingCalibration::validate(
    const ShadingCoefficients& coefficients) const {
    ShadingValidation validation;

    if (coefficients.empty() || coefficients.gain.empty()) {
        validation.reason = ShadingRejection::EmptyMeasurement;
        return validation;
    }

    std::vector<double> ranges;
    ranges.reserve(coefficients.gain.size());

    for (std::size_t i = 0; i < coefficients.gain.size(); ++i) {
        const double gain = coefficients.gain[i];

        if (!std::isfinite(gain) || gain < limits_.minGain || gain > limits_.maxGain) {
            ++validation.outliers;
        }
        validation.worstGain = std::max(validation.worstGain, std::abs(gain));

        // Odziv se rekonstruise iz pojacanja: gain = target / response.
        if (gain > 0.0 && std::isfinite(gain)) {
            ranges.push_back(1.0 / gain);
        } else {
            ranges.push_back(0.0);
        }
    }

    validation.medianDynamicRange = medianOf(ranges);

    // Skalirano na pun opseg, jer je gain izrazen u odnosu na ciljnu vrednost.
    if (validation.medianDynamicRange * kFullScale < limits_.minDynamicRange) {
        validation.reason = ShadingRejection::NoDynamicRange;
        return validation;
    }

    const double fraction = static_cast<double>(validation.outliers) /
                            static_cast<double>(coefficients.gain.size());
    if (fraction > limits_.maxOutlierFraction) {
        validation.reason = ShadingRejection::TooManyOutliers;
    }
    return validation;
}

void ShadingCalibration::apply(const ShadingCoefficients& coefficients,
                               std::size_t channel, std::span<std::uint16_t> line) {
    if (coefficients.empty() || channel >= kChannels) {
        return;
    }

    const std::size_t count = std::min(line.size(), coefficients.pixelsPerLine);
    for (std::size_t pixel = 0; pixel < count; ++pixel) {
        const double raw = static_cast<double>(line[pixel]);
        const double corrected =
            (raw - coefficients.offsetAt(channel, pixel)) * coefficients.gainAt(channel, pixel);
        line[pixel] = static_cast<std::uint16_t>(std::clamp(corrected, 0.0, kFullScale));
    }
}

}  // namespace g2710::calib
