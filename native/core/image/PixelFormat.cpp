#include "PixelFormat.h"

#include <algorithm>
#include <cmath>

namespace g2710::image {
namespace {

constexpr double kFullScale16 = 65535.0;
constexpr std::size_t kChannels = 3;

}  // namespace

const char* toString(ColorMode mode) noexcept {
    switch (mode) {
        case ColorMode::Color:   return "color";
        case ColorMode::Gray:    return "gray";
        case ColorMode::Lineart: return "lineart";
    }
    return "?";
}

LineGeometry computeLineGeometry(ColorMode mode, int depth,
                                 std::size_t widthInDots) noexcept {
    LineGeometry geometry;

    // rts8822.c:8773 - channels_per_line = channels_per_dot * width
    const std::size_t channels =
        static_cast<std::size_t>(channelsPerDot(mode)) * widthInDots;

    if (mode == ColorMode::Lineart) {
        // Jedan bit po kanalu, zaokruzeno navise na bajt.
        geometry.bytesPerLine = (channels + 7) / 8;
        geometry.depthCode = DepthCode::Lineart;
        return geometry;
    }

    switch (depth) {
        case 16:
            geometry.bytesPerLine = channels * 2;
            geometry.depthCode = DepthCode::Bits16;
            break;
        case 12:
            // 12 bita takodje zauzima dva bajta po kanalu - referenca ih ne
            // pakuje gusce.
            geometry.bytesPerLine = channels * 2;
            geometry.depthCode = DepthCode::Bits12;
            break;
        default:
            geometry.bytesPerLine = channels;
            geometry.depthCode = DepthCode::Bits8;
            break;
    }
    return geometry;
}

// --- parni i neparni ---------------------------------------------------------

Status deinterleaveEvenOdd(std::span<const std::uint16_t> interleaved,
                           std::span<std::uint16_t> evenOut,
                           std::span<std::uint16_t> oddOut) {
    const std::size_t expectedEven = (interleaved.size() + 1) / 2;
    const std::size_t expectedOdd = interleaved.size() / 2;

    if (evenOut.size() != expectedEven || oddOut.size() != expectedOdd) {
        return fail(ErrorCode::InvalidArgument, "deinterleave: pogresne duzine izlaza");
    }

    for (std::size_t i = 0; i < interleaved.size(); ++i) {
        if ((i & 1u) == 0) {
            evenOut[i / 2] = interleaved[i];
        } else {
            oddOut[i / 2] = interleaved[i];
        }
    }
    return ok();
}

Status interleaveEvenOdd(std::span<const std::uint16_t> even,
                         std::span<const std::uint16_t> odd,
                         std::span<std::uint16_t> out) {
    if (out.size() != even.size() + odd.size()) {
        return fail(ErrorCode::InvalidArgument, "interleave: pogresna duzina izlaza");
    }
    if (even.size() < odd.size() || even.size() - odd.size() > 1) {
        return fail(ErrorCode::InvalidArgument, "interleave: neuskladjene duzine");
    }

    for (std::size_t i = 0; i < out.size(); ++i) {
        out[i] = (i & 1u) == 0 ? even[i / 2] : odd[i / 2];
    }
    return ok();
}

// --- gamma -------------------------------------------------------------------

GammaTable makeGammaTable(double gamma) {
    GammaTable table(kGammaTableSize);
    const double exponent = gamma > 0.0 ? 1.0 / gamma : 1.0;

    for (std::size_t i = 0; i < kGammaTableSize; ++i) {
        const double normalised = static_cast<double>(i) / (kGammaTableSize - 1);
        const double corrected = std::pow(normalised, exponent);
        table[i] = static_cast<std::uint8_t>(
            std::clamp(std::lround(corrected * 255.0), 0L, 255L));
    }
    return table;
}

Status applyGamma(const GammaTable& table, std::span<std::uint16_t> line) {
    if (table.size() != kGammaTableSize) {
        return fail(ErrorCode::InvalidArgument, "gamma: tabela mora imati 256 ulaza");
    }

    for (std::uint16_t& value : line) {
        // Indeks iz gornjih 8 bita; rezultat nazad na punu 16-bitnu skalu, da
        // gamma ne bi tiho spustila dubinu izlaza.
        const std::size_t index = value >> 8;
        const std::uint16_t mapped = table[index];
        value = static_cast<std::uint16_t>((mapped << 8) | mapped);
    }
    return ok();
}

// --- pretvaranja -------------------------------------------------------------

Status toGrayscale(std::span<const std::uint16_t> rgb, std::size_t pixels,
                   GrayMethod method, std::span<std::uint16_t> out) {
    if (rgb.size() != kChannels * pixels) {
        return fail(ErrorCode::InvalidArgument, "gray: ulaz mora biti planarni RGB");
    }
    if (out.size() != pixels) {
        return fail(ErrorCode::InvalidArgument, "gray: pogresna duzina izlaza");
    }

    for (std::size_t i = 0; i < pixels; ++i) {
        if (method == GrayMethod::RedChannel) {
            // cfg_sensor_get channel_gray je {CL_RED, 0} - referenca za sivo
            // koristi SAMO crveni kanal, ne mesa ih.
            out[i] = rgb[i];
            continue;
        }

        const double red = rgb[i];
        const double green = rgb[pixels + i];
        const double blue = rgb[2 * pixels + i];
        const double luminance = 0.299 * red + 0.587 * green + 0.114 * blue;
        out[i] = static_cast<std::uint16_t>(std::clamp(luminance, 0.0, kFullScale16));
    }
    return ok();
}

Status toLineart(std::span<const std::uint16_t> gray, const LineartThreshold& threshold,
                 int sourceBitDepth, std::span<std::uint8_t> out) {
    const std::size_t expected = (gray.size() + 7) / 8;
    if (out.size() != expected) {
        return fail(ErrorCode::InvalidArgument, "lineart: pogresna duzina izlaza");
    }
    if (threshold.low > threshold.high) {
        return fail(ErrorCode::InvalidArgument, "lineart: donji prag je iznad gornjeg");
    }
    if (sourceBitDepth <= 0 || sourceBitDepth > 16) {
        return fail(ErrorCode::InvalidArgument, "lineart: neispravna dubina ulaza");
    }

    // Pragovi su izrazeni na 8 bita (platform parametri); skaliraj ih na
    // dubinu ulaza umesto da skaliramo svaki piksel.
    const int shift = sourceBitDepth - 8;
    const auto scale = [shift](int value) -> int {
        return shift > 0 ? (value << shift) : (value >> (-shift));
    };
    const int high = scale(threshold.high);
    const int low = scale(threshold.low);

    std::fill(out.begin(), out.end(), std::uint8_t{0});

    bool previousWasWhite = false;
    for (std::size_t i = 0; i < gray.size(); ++i) {
        const int value = gray[i];

        bool white;
        if (value >= high) {
            white = true;
        } else if (value <= low) {
            white = false;
        } else {
            // Izmedju pragova odluka se zadrzava - histereza. Sa jednim
            // pragom bi granicni pikseli davali sum.
            white = previousWasWhite;
        }
        previousWasWhite = white;

        if (white) {
            // Najvisi bit prvi, kao sto cip isporucuje.
            out[i / 8] = static_cast<std::uint8_t>(out[i / 8] | (0x80u >> (i % 8)));
        }
    }
    return ok();
}

// --- dubina ------------------------------------------------------------------

void reduceTo8Bit(std::span<const std::uint16_t> in, std::span<std::uint8_t> out) {
    const std::size_t count = std::min(in.size(), out.size());
    for (std::size_t i = 0; i < count; ++i) {
        // Zaokruzivanje, ne odsecanje: 0xFF80 mora dati 0xFF, ne 0xFF.
        out[i] = static_cast<std::uint8_t>((in[i] * 255 + 32767) / 65535);
    }
}

}  // namespace g2710::image
