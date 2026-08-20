#include "VirtualCcd.h"

#include <algorithm>
#include <cmath>

namespace g2710::sim {
namespace {

// Deterministicki "sum" po indeksu piksela. Nije generator slucajnih brojeva:
// vrednost zavisi samo od indeksa, pa isti piksel uvek ima istu manu - kao
// pravi senzor, cije je odstupanje fiksni obrazac, a ne buka.
double fixedPattern(std::size_t pixel, std::uint32_t salt) noexcept {
    std::uint32_t h = static_cast<std::uint32_t>(pixel) * 2654435761u + salt;
    h ^= h >> 15;
    h *= 2246822519u;
    h ^= h >> 13;
    h *= 3266489917u;
    h ^= h >> 16;
    // -1.0 .. 1.0
    return (static_cast<double>(h) / 2147483647.5) - 1.0;
}

constexpr double kFullScale = 65535.0;

}  // namespace

void VirtualCcd::makeIdeal() noexcept {
    imperfections_.pixelGainSpread = 0.0;
    imperfections_.darkOffsetBase = 0.0;
    imperfections_.darkOffsetSpread = 0.0;
    imperfections_.evenOddGainDelta = 0.0;
    imperfections_.evenOddOffsetDelta = 0.0;
    imperfections_.lampFalloff = 0.0;
}

std::size_t VirtualCcd::pixelsPerLine(int resolution) const noexcept {
    if (resolution <= 0) {
        return 0;
    }
    const double inches = geometry_.widthMm / 25.4;
    return static_cast<std::size_t>(inches * resolution);
}

int VirtualCcd::channelRowOffset(CcdChannel channel, int resolution) const noexcept {
    if (resolution <= 0 || geometry_.sensorResolution <= 0) {
        return 0;
    }
    // line_distance je izrazen na 2400 dpi; skaliraj na rezoluciju MOTORA,
    // jer se pozicija glave meri u koracima motora.
    const double scaled = static_cast<double>(imperfections_.lineDistanceAt2400) *
                          static_cast<double>(geometry_.motorResolution) /
                          static_cast<double>(geometry_.sensorResolution);
    return static_cast<int>(std::lround(scaled)) * static_cast<int>(channel);
}

double VirtualCcd::pixelGain(std::size_t pixel) const noexcept {
    double gain = 1.0 + imperfections_.pixelGainSpread * fixedPattern(pixel, 0x9E3779B9u);
    // Parni i neparni piksel idu kroz razlicite ADC lance.
    if ((pixel & 1u) == 0) {
        gain += imperfections_.evenOddGainDelta;
    } else {
        gain -= imperfections_.evenOddGainDelta;
    }
    return std::max(gain, 0.0);
}

double VirtualCcd::pixelDarkOffset(std::size_t pixel) const noexcept {
    double offset = imperfections_.darkOffsetBase +
                    imperfections_.darkOffsetSpread * fixedPattern(pixel, 0x85EBCA6Bu);
    if ((pixel & 1u) == 0) {
        offset += imperfections_.evenOddOffsetDelta;
    } else {
        offset -= imperfections_.evenOddOffsetDelta;
    }
    return std::max(offset, 0.0);
}

void VirtualCcd::readLine(int motorPosition, int resolution, CcdChannel channel,
                          const VirtualLamp& lamp, std::span<std::uint16_t> out) const {
    if (resolution <= 0 || out.empty()) {
        return;
    }

    // Red mete koji ovaj kanal zaista vidi.
    const int row = motorPosition + channelRowOffset(channel, resolution);
    const double yMm = geometry_.motorResolution > 0
                           ? (static_cast<double>(row) / geometry_.motorResolution) * 25.4
                           : 0.0;

    const double lampLevel = lamp.level();
    const std::size_t count = out.size();
    const auto channelIndex = static_cast<std::size_t>(channel);

    for (std::size_t pixel = 0; pixel < count; ++pixel) {
        // Uzorkuje se CENTAR piksela, ne njegova ivica.
        //
        // Sa krajnjim tackama poslednji piksel pada tacno na x = sirina, sto
        // je vec izvan mete, pa bi svaki red imao crn poslednji piksel.
        // Konvencija centra drzi sve uzorke unutar povrsine.
        const double fraction = (static_cast<double>(pixel) + 0.5) /
                                static_cast<double>(count);
        const double xMm = fraction * geometry_.widthMm;

        const Reflectance reflectance = sampleTestTarget(xMm, yMm);

        // Svetlo pada ka ivicama reda.
        const double fromCentre = std::abs(fraction - 0.5) * 2.0;
        const double illumination =
            lampLevel * (1.0 - imperfections_.lampFalloff * fromCentre * fromCentre);

        const double signal = illumination * reflectance.channel(channelIndex) *
                              pixelGain(pixel) / kFullScale * kFullScale;

        const double value = signal + pixelDarkOffset(pixel);
        out[pixel] = static_cast<std::uint16_t>(std::clamp(value, 0.0, kFullScale));
    }
}

}  // namespace g2710::sim
