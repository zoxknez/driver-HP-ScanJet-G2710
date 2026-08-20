#include "Capabilities.h"

#include "G2710Profile.generated.h"
#include "image/LineOffset.h"

#include <algorithm>

namespace g2710::scan {
namespace {

// ST_NORMAL iz hp3900_types.c.
constexpr int kFlatbedScanType = 1;

// Tabela je RUCNO odrzavana jer nosi odluke, ne podatke. Sta hardver ima stoji
// u profilu; sta smemo da tvrdimo stoji ovde.
//
// Native rezolucije za HPG2710 flatbed u hp3800_scanmodes su tacno cetiri plus
// dve zakljucane: 150, 300, 600, 1200, 2400. Reference izlaze SANE-u sedam
// vrednosti (bknd_resolutions, hp3900_sane.c:284); razlika se dobija
// smanjivanjem iz najblize vise native rezolucije, kao u Scanmode_fitres.
constexpr ResolutionCapability kFlatbed[] = {
    {50, ResolutionOrigin::Resized, ValidationLevel::ReferenceValidated, 150,
     "smanjivanje iz 150 dpi; nema native red"},

    {75, ResolutionOrigin::Resized, ValidationLevel::ReferenceValidated, 150,
     "smanjivanje iz 150 dpi; nema native red"},

    {100, ResolutionOrigin::Resized, ValidationLevel::ReferenceValidated, 150,
     "smanjivanje iz 150 dpi; nema native red"},

    {150, ResolutionOrigin::Native, ValidationLevel::ReferenceValidated, 0,
     "najniza native rezolucija"},

    {200, ResolutionOrigin::Resized, ValidationLevel::ReferenceValidated, 300,
     "smanjivanje iz 300 dpi; nema native red"},

    {300, ResolutionOrigin::Native, ValidationLevel::ReferenceValidated, 0, ""},

    {600, ResolutionOrigin::Native, ValidationLevel::ReferenceValidated, 0,
     "najvisa rezolucija na kojoj hardversko poravnanje redova jos staje u 6 bita"},

    {1200, ResolutionOrigin::Native, ValidationLevel::Implemented, 0,
     "defekt D3: pomak plavog kanala se preliva iz 6-bitnog polja; ceka H8"},

    {2400, ResolutionOrigin::Native, ValidationLevel::Implemented, 0,
     "defekt D3: prelivaju se i zeleni i plavi; ceka H8"},
};

constexpr DepthCapability kDepths[] = {
    {8, ValidationLevel::ReferenceValidated, ""},
    {16, ValidationLevel::Implemented,
     "48-bit izlaz je qualification-gated; ceka H8"},
};

template <typename T, std::size_t N>
constexpr std::size_t countOf(const T (&)[N]) {
    return N;
}

}  // namespace

const char* toString(ValidationLevel level) noexcept {
    switch (level) {
        case ValidationLevel::NotImplemented:     return "NOT_IMPLEMENTED";
        case ValidationLevel::Implemented:        return "IMPLEMENTED";
        case ValidationLevel::ReferenceValidated: return "REFERENCE_VALIDATED";
        case ValidationLevel::HardwareValidated:  return "HARDWARE_VALIDATED";
    }
    return "?";
}

const char* toString(ResolutionOrigin origin) noexcept {
    switch (origin) {
        case ResolutionOrigin::Native:  return "native";
        case ResolutionOrigin::Resized: return "resize";
    }
    return "?";
}

std::span<const ResolutionCapability> flatbedResolutions() noexcept {
    return {kFlatbed, countOf(kFlatbed)};
}

std::vector<int> advertisableResolutions() {
    std::vector<int> result;
    for (const auto& capability : kFlatbed) {
        if (capability.advertisable()) {
            result.push_back(capability.dpi);
        }
    }
    return result;
}

std::vector<int> executableResolutions() {
    std::vector<int> result;
    for (const auto& capability : kFlatbed) {
        if (capability.level != ValidationLevel::NotImplemented) {
            result.push_back(capability.dpi);
        }
    }
    return result;
}

const ResolutionCapability* findResolution(int dpi) noexcept {
    for (const auto& capability : kFlatbed) {
        if (capability.dpi == dpi) {
            return &capability;
        }
    }
    return nullptr;
}

std::span<const DepthCapability> depthCapabilities() noexcept {
    return {kDepths, countOf(kDepths)};
}

image::ColorMode tableColorMode(image::ColorMode mode) noexcept {
    // U hp3800_scanmodes NEMA nijednog lineart reda. Referenca to resava
    // padom na sivo (RTS_GetScanmode, rts8822.c:2167), pa i mi.
    return mode == image::ColorMode::Lineart ? image::ColorMode::Gray : mode;
}

int maxNativeResolution(image::ColorMode mode) noexcept {
    const int wanted = static_cast<int>(tableColorMode(mode));
    int best = 0;
    for (const auto& row : profile::kScanModes) {
        if (row.mode.colorMode != wanted) {
            continue;
        }
        if (row.mode.scanType != kFlatbedScanType) {
            continue;
        }
        best = std::max(best, row.mode.resolution);
    }
    return best;
}

int minNativeResolution(image::ColorMode mode) noexcept {
    const int wanted = static_cast<int>(tableColorMode(mode));
    int best = 0;
    for (const auto& row : profile::kScanModes) {
        if (row.mode.colorMode != wanted) {
            continue;
        }
        if (row.mode.scanType != kFlatbedScanType) {
            continue;
        }
        if (best == 0 || row.mode.resolution < best) {
            best = row.mode.resolution;
        }
    }
    return best;
}

}  // namespace g2710::scan
