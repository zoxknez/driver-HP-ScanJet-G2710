#include "TestTarget.h"

#include <cmath>

namespace g2710::sim {
namespace {

// Osam traka: R, G, B, C, M, Y, bela, crna.
Reflectance colorBar(int index) noexcept {
    constexpr double hi = 0.90;
    constexpr double lo = 0.04;

    switch (index) {
        case 0: return {hi, lo, lo};
        case 1: return {lo, hi, lo};
        case 2: return {lo, lo, hi};
        case 3: return {lo, hi, hi};
        case 4: return {hi, lo, hi};
        case 5: return {hi, hi, lo};
        case 6: return {hi, hi, hi};
        default: return {lo, lo, lo};
    }
}

bool onGridLine(double value) noexcept {
    const double offset = std::fmod(value, target::kGridPitchMm);
    return offset < target::kGridLineMm;
}

}  // namespace

Reflectance sampleTestTarget(double xMillimetres, double yMillimetres) noexcept {
    if (xMillimetres < 0.0 || xMillimetres >= target::kWidthMm ||
        yMillimetres < 0.0 || yMillimetres >= target::kHeightMm) {
        return {0.0, 0.0, 0.0};
    }

    // Kalibracione trake.
    if (yMillimetres < target::kWhiteStripBottom) {
        const double v = target::kWhiteReflectance;
        return {v, v, v};
    }
    if (yMillimetres < target::kBlackStripBottom) {
        const double v = target::kBlackReflectance;
        return {v, v, v};
    }

    // Trake u boji.
    if (yMillimetres < target::kColorBarsBottom) {
        const double barWidth = target::kWidthMm / target::kColorBarCount;
        auto index = static_cast<int>(xMillimetres / barWidth);
        if (index >= target::kColorBarCount) {
            index = target::kColorBarCount - 1;
        }
        return colorBar(index);
    }

    // Horizontalni gradijent. Linearan u refleksiji, pa svaka nelinearnost u
    // rekonstrukciji dolazi od nas, ne od mete.
    if (yMillimetres < target::kGradientBottom) {
        const double v = xMillimetres / target::kWidthMm;
        return {v, v, v};
    }

    // Geometrijska mreza.
    if (yMillimetres < target::kGridBottom) {
        const bool line = onGridLine(xMillimetres) ||
                          onGridLine(yMillimetres - target::kGradientBottom);
        const double v = line ? target::kBlackReflectance : target::kWhiteReflectance;
        return {v, v, v};
    }

    const double v = target::kNeutralReflectance;
    return {v, v, v};
}

}  // namespace g2710::sim
