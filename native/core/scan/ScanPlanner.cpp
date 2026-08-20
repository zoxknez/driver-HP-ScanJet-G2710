#include "ScanPlanner.h"

#include <algorithm>
#include <cstddef>
#include <iterator>

namespace g2710::scan {
namespace {

constexpr int kFlatbedScanType = 1;

// PIXEL_RATE = 0x00 u hp3900_types.c:75.
constexpr int kPixelRate = 0;

// Nadji tacan red u hp3800_scanmodes.
int findScanMode(int resolution, image::ColorMode tableMode, ScanSource source,
                 UsbSpeed usb) noexcept {
    for (std::size_t i = 0; i < std::size(profile::kScanModes); ++i) {
        const auto& row = profile::kScanModes[i];
        if (row.usb != static_cast<int>(usb)) {
            continue;
        }
        if (row.mode.scanType != static_cast<int>(source)) {
            continue;
        }
        if (row.mode.colorMode != static_cast<int>(tableMode)) {
            continue;
        }
        if (row.mode.resolution != resolution) {
            continue;
        }
        return static_cast<int>(i);
    }
    return -1;
}

// Scanmode_fitres, rts8822.c:2091 - najmanja native rezolucija koja NIJE manja
// od trazene. Nula ako je trazena veca od svega sto tabela ima.
int fitResolution(int requested, image::ColorMode tableMode) noexcept {
    int best = 0;
    for (const auto& row : profile::kScanModes) {
        if (row.mode.scanType != kFlatbedScanType) {
            continue;
        }
        if (row.mode.colorMode != static_cast<int>(tableMode)) {
            continue;
        }
        if (row.mode.resolution < requested) {
            continue;
        }
        if (best == 0 || row.mode.resolution < best) {
            best = row.mode.resolution;
        }
    }
    return best;
}

// Skaliranje koordinata iz rts8822.c:1710. Visina dobija +2 reda - referenca
// tako obezbedjuje da smanjivanju ne ponestane ulaznih redova na kraju slike.
ScanRegion scaleRegion(const ScanRegion& region, int fromResolution,
                       int toResolution) noexcept {
    if (fromResolution == toResolution) {
        return region;
    }
    ScanRegion scaled;
    scaled.left = (toResolution * region.left) / fromResolution;
    scaled.width = (toResolution * region.width) / fromResolution;
    scaled.top = (toResolution * region.top) / fromResolution;
    scaled.height = ((toResolution * region.height) / fromResolution) + 2;
    return scaled;
}

}  // namespace

const char* toString(ScanSource source) noexcept {
    switch (source) {
        case ScanSource::Flatbed:  return "flatbed";
        case ScanSource::Tma:      return "TMA";
        case ScanSource::Negative: return "negativ";
    }
    return "?";
}

const char* toString(ResizeType type) noexcept {
    switch (type) {
        case ResizeType::None:     return "none";
        case ResizeType::Decrease: return "decrease";
        case ResizeType::Increase: return "increase";
    }
    return "?";
}

const profile::ScanMode& ScanPlan::scanMode() const {
    return profile::kScanModes[static_cast<std::size_t>(scanModeIndex)].mode;
}

ScanRegion fullFlatbedRegion(int resolution) noexcept {
    ScanRegion region;
    if (resolution <= 0) {
        return region;
    }
    // kConstraints je u milimetrima (hp3900_config.c, cfg_constrains_get).
    const double inchesWide = profile::kConstraints.reflective.width / 25.4;
    const double inchesTall = profile::kConstraints.reflective.height / 25.4;

    region.width = static_cast<int>(inchesWide * resolution);
    region.height = static_cast<int>(inchesTall * resolution);
    return region;
}

Result<ScanPlan> planScan(const ScanRequest& request) {
    if (request.source != ScanSource::Flatbed) {
        // TMA tabele postoje, ali nose defekte D1 i D2 i nisu kvalifikovane.
        return fail(ErrorCode::NotImplementedIn10, "planScan: samo flatbed u 1.0");
    }
    if (request.depth != 8 && request.depth != 16) {
        return fail(ErrorCode::InvalidArgument, "planScan: dubina mora biti 8 ili 16");
    }

    const ResolutionCapability* capability = findResolution(request.resolution);
    if (capability == nullptr) {
        return fail(ErrorCode::InvalidArgument, "planScan: nepoznata rezolucija");
    }
    if (capability->level == ValidationLevel::NotImplemented) {
        return fail(ErrorCode::NotImplementedIn10, "planScan: rezolucija nije implementirana");
    }
    if (!capability->advertisable() && !request.allowUnqualified) {
        // Kapija iz MASTER plana. Rezolucija POSTOJI u kodu, ali dok H8 ne
        // potvrdi, ne nudi se - vidi Capabilities.h.
        return fail(ErrorCode::NotImplementedIn10,
                    "planScan: rezolucija nije hardverski potvrdjena");
    }

    ScanPlan plan;
    plan.requestedResolution = request.resolution;
    plan.tableMode = tableColorMode(request.colorMode);
    plan.hardwareDepth = request.colorMode == image::ColorMode::Lineart ? 8 : request.depth;

    // Prvo se trazi tacan red; tek ako ga nema, ide se na promenu velicine.
    plan.scanModeIndex =
        findScanMode(request.resolution, plan.tableMode, request.source, request.usb);
    if (plan.scanModeIndex >= 0) {
        plan.nativeResolution = request.resolution;
        plan.resize = ResizeType::None;
    } else {
        int fit = fitResolution(request.resolution, plan.tableMode);
        if (fit != 0) {
            plan.resize = ResizeType::Decrease;
        } else {
            // Iznad svega sto tabela ima; referenca tada skenira na maksimumu i
            // uvecava. Za G2710 je nedostizno - 2400 je i najveca u tabeli i
            // najveca u tabeli mogucnosti - ali put postoji jer postoji i tamo.
            plan.resize = ResizeType::Increase;
            fit = maxNativeResolution(request.colorMode);
        }
        plan.nativeResolution = fit;
        plan.scanModeIndex =
            findScanMode(fit, plan.tableMode, request.source, request.usb);
    }

    if (plan.scanModeIndex < 0 || plan.nativeResolution <= 0) {
        return fail(ErrorCode::InvalidArgument,
                    "planScan: nema reda u tabeli za taj rezim i rezoluciju");
    }

    const profile::ScanMode& mode = plan.scanMode();
    plan.timingIndex = mode.timing;
    plan.motorCurveIndex = mode.motorCurve;
    plan.sampleRate = mode.sampleRate;
    plan.systemClock = mode.systemClock;
    plan.ctpc = mode.ctpc;
    plan.motorBackStep = mode.motorBackStep;

    plan.requestedRegion = request.region.empty() ? fullFlatbedRegion(request.resolution)
                                                  : request.region;

    const ScanRegion surface = fullFlatbedRegion(request.resolution);
    if (plan.requestedRegion.left < 0 || plan.requestedRegion.top < 0 ||
        plan.requestedRegion.left + plan.requestedRegion.width > surface.width ||
        plan.requestedRegion.top + plan.requestedRegion.height > surface.height) {
        return fail(ErrorCode::InvalidArgument, "planScan: oblast izlazi van povrsine");
    }

    plan.nativeRegion =
        scaleRegion(plan.requestedRegion, request.resolution, plan.nativeResolution);

    plan.grayPixelRateDoubling =
        plan.tableMode == image::ColorMode::Gray && mode.sampleRate == kPixelRate;

    const std::size_t hardwareWidth =
        static_cast<std::size_t>(plan.nativeRegion.width) *
        (plan.grayPixelRateDoubling ? 2u : 1u);

    plan.hardwareLine =
        image::computeLineGeometry(plan.tableMode, plan.hardwareDepth, hardwareWidth);
    plan.outputLine = image::computeLineGeometry(
        request.colorMode, request.depth,
        static_cast<std::size_t>(plan.requestedRegion.width));

    // Poravnanje kanala se racuna na NATIVE rezoluciji - tu se i skenira.
    const bool highResolution = plan.nativeResolution > 1200;
    plan.lineOffsets = image::computeLineOffsets(
        profile::kSensor.lineDistance, profile::kSensor.evenOddDistance,
        profile::kSensor.resolution, plan.nativeResolution, highResolution);

    // Poravnanje se tice samo boje; sivo i lineart citaju jedan kanal.
    if (plan.tableMode == image::ColorMode::Color) {
        // Hardversko samo ako sve staje u 6 bita; inace softversko.
        // Vidi D3 u docs/REFERENCE-DEFECTS.md.
        plan.useHardwareAlignment = plan.lineOffsets.fitsInHardware();
        if (!plan.useHardwareAlignment) {
            plan.softwareLineDistance = plan.lineOffsets.lineDistance;
            plan.alignmentLookahead = plan.softwareLineDistance * 2;
        }
    }

    return plan;
}

}  // namespace g2710::scan
