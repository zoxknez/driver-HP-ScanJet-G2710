#include "WiaCapabilities.h"

#include "G2710Profile.generated.h"

#include <algorithm>

namespace g2710::wia {
namespace {

constexpr WiaDataType kDataTypes[] = {
    WiaDataType::Color,
    WiaDataType::Grayscale,
    WiaDataType::Threshold,
};

// WIA_IPA_DEPTH je po TACKI. Boja: 24 ili 48. Sivo: 8 ili 16. Lineart: 1.
constexpr int kColorDepths[] = {24, 48};
constexpr int kGrayDepths[] = {8, 16};
constexpr int kLineartDepths[] = {1};

// Rezolucija koju biramo kada je ponudjena. Ni najniza ni najvisa - ona koju
// korisnik najcesce hoce.
constexpr int kPreferredResolution = 300;

std::vector<int> unqualifiedResolutions() {
    // Sve sto kod ume da izvrsi, ukljucujuci nepotvrdjeno.
    std::vector<int> result;
    for (int dpi : scan::executableResolutions()) {
        const scan::ResolutionCapability* capability = scan::findResolution(dpi);
        if (capability == nullptr) {
            continue;
        }
        // 1200 i 2400 nose D3 i ne idu ni u kvalifikacioni paket bez potrebe;
        // H8 ih otvara posebno.
        if (capability->level == scan::ValidationLevel::ReferenceValidated) {
            result.push_back(dpi);
        }
    }
    return result;
}

}  // namespace

const char* toString(WiaDataType type) noexcept {
    switch (type) {
        case WiaDataType::Threshold: return "threshold";
        case WiaDataType::Grayscale: return "grayscale";
        case WiaDataType::Color:     return "color";
    }
    return "?";
}

const char* toString(Rejection reason) noexcept {
    switch (reason) {
        case Rejection::None:                  return "prihvaceno";
        case Rejection::ResolutionNotOffered:  return "rezolucija se ne nudi";
        case Rejection::ResolutionsDiffer:     return "X i Y rezolucija se razlikuju";
        case Rejection::DepthNotOffered:       return "dubina se ne nudi";
        case Rejection::RegionOutsideSurface:  return "oblast izlazi van povrsine";
        case Rejection::EmptyRegion:           return "prazna oblast";
        case Rejection::NoUsableConfiguration: return "nema hardverski potvrdjene rezolucije";
    }
    return "?";
}

image::ColorMode toColorMode(WiaDataType type) noexcept {
    switch (type) {
        case WiaDataType::Threshold: return image::ColorMode::Lineart;
        case WiaDataType::Grayscale: return image::ColorMode::Gray;
        case WiaDataType::Color:     return image::ColorMode::Color;
    }
    return image::ColorMode::Color;
}

WiaDataType toWiaDataType(image::ColorMode mode) noexcept {
    switch (mode) {
        case image::ColorMode::Lineart: return WiaDataType::Threshold;
        case image::ColorMode::Gray:    return WiaDataType::Grayscale;
        case image::ColorMode::Color:   return WiaDataType::Color;
    }
    return WiaDataType::Color;
}

int wiaDepthFor(image::ColorMode mode, int bitsPerChannel) noexcept {
    if (mode == image::ColorMode::Lineart) {
        return 1;
    }
    return image::channelsPerDot(mode) * bitsPerChannel;
}

int bitsPerChannelFrom(image::ColorMode mode, int wiaDepth) noexcept {
    if (mode == image::ColorMode::Lineart) {
        // Lineart se skenira kao osmobitno sivo pa pragom svodi na bit.
        return 8;
    }
    const int channels = image::channelsPerDot(mode);
    return channels > 0 ? wiaDepth / channels : 8;
}

std::vector<int> offeredResolutions() {
    std::vector<int> advertised = scan::advertisableResolutions();
    if (!advertised.empty()) {
        return advertised;
    }
    if (kAllowUnqualified) {
        return unqualifiedResolutions();
    }
    // Izdanje pre kvalifikacije: nema sta da se ponudi, i to se ne krije.
    return {};
}

bool hasUsableConfiguration() { return !offeredResolutions().empty(); }

int defaultResolution() {
    const std::vector<int> offered = offeredResolutions();
    if (offered.empty()) {
        return 0;
    }
    if (std::find(offered.begin(), offered.end(), kPreferredResolution) != offered.end()) {
        return kPreferredResolution;
    }
    return offered.front();
}

std::span<const WiaDataType> offeredDataTypes() noexcept {
    return {kDataTypes, std::size(kDataTypes)};
}

std::span<const int> offeredBitDepths(WiaDataType type) noexcept {
    switch (type) {
        case WiaDataType::Color:     return {kColorDepths, std::size(kColorDepths)};
        case WiaDataType::Grayscale: return {kGrayDepths, std::size(kGrayDepths)};
        case WiaDataType::Threshold: return {kLineartDepths, std::size(kLineartDepths)};
    }
    return {};
}

SurfaceMillimetres flatbedSurface() noexcept {
    return {profile::kConstraints.reflective.width, profile::kConstraints.reflective.height};
}

ItemSettings defaultSettings() {
    ItemSettings settings;
    settings.xResolution = defaultResolution();
    settings.yResolution = settings.xResolution;
    settings.dataType = WiaDataType::Color;
    settings.wiaDepth = 24;

    const scan::ScanRegion full = scan::fullFlatbedRegion(settings.xResolution);
    settings.xPosition = 0;
    settings.yPosition = 0;
    settings.xExtent = full.width;
    settings.yExtent = full.height;
    return settings;
}

Validation validate(const ItemSettings& settings) {
    Validation result;
    result.corrected = settings;

    const std::vector<int> offered = offeredResolutions();
    if (offered.empty()) {
        result.reason = Rejection::NoUsableConfiguration;
        return result;
    }

    // WIA dozvoljava razlicite X i Y rezolucije; nas hardver ne. Umesto da se
    // zahtev odbije, izjednacavaju se - to je ono sto drvValidateItemProperties
    // treba da uradi.
    if (settings.xResolution != settings.yResolution) {
        result.corrected.yResolution = result.corrected.xResolution;
        result.changed = true;
    }

    if (std::find(offered.begin(), offered.end(), result.corrected.xResolution) ==
        offered.end()) {
        result.reason = Rejection::ResolutionNotOffered;
        return result;
    }

    const std::span<const int> depths = offeredBitDepths(result.corrected.dataType);
    if (std::find(depths.begin(), depths.end(), result.corrected.wiaDepth) == depths.end()) {
        // Lineart ima tacno jednu dubinu, pa se tiho ispravlja; za ostalo je
        // pogresna dubina stvarna greska aplikacije.
        if (result.corrected.dataType == WiaDataType::Threshold) {
            result.corrected.wiaDepth = 1;
            result.changed = true;
        } else {
            result.reason = Rejection::DepthNotOffered;
            return result;
        }
    }

    const scan::ScanRegion surface = scan::fullFlatbedRegion(result.corrected.xResolution);

    if (result.corrected.extentUnset()) {
        result.corrected.xExtent = surface.width;
        result.corrected.yExtent = surface.height;
        result.changed = true;
    }

    if (result.corrected.xPosition < 0) {
        result.corrected.xPosition = 0;
        result.changed = true;
    }
    if (result.corrected.yPosition < 0) {
        result.corrected.yPosition = 0;
        result.changed = true;
    }
    if (result.corrected.xPosition >= surface.width ||
        result.corrected.yPosition >= surface.height) {
        result.reason = Rejection::RegionOutsideSurface;
        return result;
    }

    // Prekoracenje se SECE na povrsinu. Aplikacija koja trazi vise nego sto
    // staklo ima nije pogresila - samo ne zna granicu.
    if (result.corrected.xPosition + result.corrected.xExtent > surface.width) {
        result.corrected.xExtent = surface.width - result.corrected.xPosition;
        result.changed = true;
    }
    if (result.corrected.yPosition + result.corrected.yExtent > surface.height) {
        result.corrected.yExtent = surface.height - result.corrected.yPosition;
        result.changed = true;
    }

    if (result.corrected.xExtent <= 0 || result.corrected.yExtent <= 0) {
        result.reason = Rejection::EmptyRegion;
        return result;
    }

    return result;
}

Result<scan::ScanRequest> toScanRequest(const ItemSettings& settings) {
    const Validation checked = validate(settings);
    if (!checked.accepted()) {
        return fail(ErrorCode::InvalidArgument, toString(checked.reason));
    }

    const ItemSettings& good = checked.corrected;
    const image::ColorMode mode = toColorMode(good.dataType);

    scan::ScanRequest request;
    request.resolution = good.xResolution;
    request.colorMode = mode;
    request.depth = bitsPerChannelFrom(mode, good.wiaDepth);
    request.source = scan::ScanSource::Flatbed;
    request.region = {good.xPosition, good.yPosition, good.xExtent, good.yExtent};

    // Jedino mesto u WIA sloju gde se kapija otvara, i samo u kvalifikacionom
    // build-u. U izdanju offeredResolutions() ionako ne bi propustila
    // nepotvrdjenu vrednost dovde.
    request.allowUnqualified = kAllowUnqualified;
    return request;
}

Result<std::uint64_t> imageSizeInBytes(const ItemSettings& settings) {
    const Validation checked = validate(settings);
    if (!checked.accepted()) {
        return fail(ErrorCode::InvalidArgument, toString(checked.reason));
    }

    const ItemSettings& good = checked.corrected;
    const image::ColorMode mode = toColorMode(good.dataType);
    const image::LineGeometry geometry = image::computeLineGeometry(
        mode, bitsPerChannelFrom(mode, good.wiaDepth),
        static_cast<std::size_t>(good.xExtent));

    return static_cast<std::uint64_t>(geometry.bytesPerLine) *
           static_cast<std::uint64_t>(good.yExtent);
}

}  // namespace g2710::wia
