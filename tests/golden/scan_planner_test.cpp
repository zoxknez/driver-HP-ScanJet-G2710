// Planer i tabela mogucnosti.
//
// Dve grupe testova nose razlicit teret.
//
// Prva zakljucava ono sto smo PROCITALI iz reference: koje rezolucije zaista
// imaju red u hp3800_scanmodes, da lineart redova nema uopste, i sta se desava
// kada trazena rezolucija nije u tabeli. Ako se generisani profil promeni,
// ovde puca.
//
// Druga zakljucava ono sto smo ODLUCILI: da WIA i TWAIN ne oglasavaju nista sto
// nije prosla hardverska kvalifikacija. Trenutno to znaci - nista. Test koji
// pada u trenutku kada neko ubaci prvu HARDWARE_VALIDATED vrednost bez
// pratecih H rezultata je i poenta.

#include "G2710Profile.generated.h"
#include "image/PixelFormat.h"
#include "scan/Capabilities.h"
#include "scan/ScanPlanner.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <set>
#include <vector>

using namespace g2710;
using namespace g2710::scan;
using g2710::image::ColorMode;

namespace {

constexpr int kFlatbedScanType = 1;

ScanRequest flatbedRequest(int dpi, ColorMode mode, int depth = 8) {
    ScanRequest request;
    request.resolution = dpi;
    request.colorMode = mode;
    request.depth = depth;
    request.allowUnqualified = true;  // inace ne prolazi nista - vidi CapabilityGate
    return request;
}

std::set<int> nativeResolutionsFor(int colorMode) {
    std::set<int> result;
    for (const auto& row : profile::kScanModes) {
        if (row.usb == 1 && row.mode.scanType == kFlatbedScanType &&
            row.mode.colorMode == colorMode) {
            result.insert(row.mode.resolution);
        }
    }
    return result;
}

}  // namespace

// --- sta tabela zaista sadrzi -----------------------------------------------

TEST(ScanModeTable, FlatbedHasFiveNativeResolutions) {
    const std::set<int> expected{150, 300, 600, 1200, 2400};
    EXPECT_EQ(nativeResolutionsFor(0), expected) << "boja";
    EXPECT_EQ(nativeResolutionsFor(1), expected) << "sivo";
}

// Ovo je razlog zasto planer uopste ima tableColorMode().
TEST(ScanModeTable, HasNoLineartRowsAtAll) {
    for (const auto& row : profile::kScanModes) {
        EXPECT_NE(row.mode.colorMode, static_cast<int>(ColorMode::Lineart));
    }
    EXPECT_EQ(tableColorMode(ColorMode::Lineart), ColorMode::Gray);
    EXPECT_EQ(tableColorMode(ColorMode::Gray), ColorMode::Gray);
    EXPECT_EQ(tableColorMode(ColorMode::Color), ColorMode::Color);
}

// Lineart preuzima granice sivog jer preuzima i njegove redove.
TEST(ScanModeTable, LineartInheritsGrayLimits) {
    EXPECT_EQ(maxNativeResolution(ColorMode::Lineart), maxNativeResolution(ColorMode::Gray));
    EXPECT_EQ(minNativeResolution(ColorMode::Lineart), minNativeResolution(ColorMode::Gray));
    EXPECT_EQ(maxNativeResolution(ColorMode::Color), 2400);
    EXPECT_EQ(minNativeResolution(ColorMode::Color), 150);
}

// --- tabela mogucnosti -------------------------------------------------------

TEST(Capabilities, MatchesReferenceExposedListPlusTwoGatedValues) {
    // bknd_resolutions, hp3900_sane.c:284 - referenca izlaze tacno ovo.
    const std::vector<int> exposedByReference{50, 75, 100, 150, 200, 300, 600};

    std::vector<int> referenceValidated;
    std::vector<int> gated;
    for (const auto& capability : flatbedResolutions()) {
        if (capability.level == ValidationLevel::ReferenceValidated) {
            referenceValidated.push_back(capability.dpi);
        } else if (capability.level == ValidationLevel::Implemented) {
            gated.push_back(capability.dpi);
        }
    }

    EXPECT_EQ(referenceValidated, exposedByReference);
    EXPECT_EQ(gated, (std::vector<int>{1200, 2400}));
}

TEST(Capabilities, ResizedEntriesPointAtTheNextNativeResolutionUp) {
    for (const auto& capability : flatbedResolutions()) {
        if (capability.origin != ResolutionOrigin::Resized) {
            EXPECT_EQ(capability.sourceDpi, 0) << capability.dpi;
            continue;
        }
        EXPECT_GT(capability.sourceDpi, capability.dpi);
        EXPECT_TRUE(nativeResolutionsFor(0).count(capability.sourceDpi) == 1)
            << "izvor " << capability.sourceDpi << " nije native";
    }
}

// Native znaci native - ne "pisemo da jeste".
TEST(Capabilities, NativeEntriesReallyHaveARow) {
    const std::set<int> native = nativeResolutionsFor(0);
    for (const auto& capability : flatbedResolutions()) {
        const bool hasRow = native.count(capability.dpi) == 1;
        EXPECT_EQ(capability.origin == ResolutionOrigin::Native, hasRow)
            << capability.dpi << " dpi";
    }
}

TEST(Capabilities, DepthsAreEightAndSixteenAndOnlyEightIsReferenceValidated) {
    const auto depths = depthCapabilities();
    ASSERT_EQ(depths.size(), 2u);
    EXPECT_EQ(depths[0].bits, 8);
    EXPECT_EQ(depths[0].level, ValidationLevel::ReferenceValidated);
    EXPECT_EQ(depths[1].bits, 16);
    EXPECT_EQ(depths[1].level, ValidationLevel::Implemented);
}

// --- kapija ------------------------------------------------------------------

// Ovaj test pada cim neko oznaci bilo sta kao HARDWARE_VALIDATED. To je
// namerno: takva izmena sme da udje samo zajedno sa H rezultatima.
TEST(CapabilityGate, NothingIsAdvertisableYet) {
    EXPECT_TRUE(advertisableResolutions().empty())
        << "WIA i TWAIN bi oglasili rezoluciju koju hardver nije potvrdio";

    for (const auto& capability : flatbedResolutions()) {
        EXPECT_NE(capability.level, ValidationLevel::HardwareValidated) << capability.dpi;
    }
}

TEST(CapabilityGate, EverythingIsStillExecutable) {
    EXPECT_EQ(executableResolutions(),
              (std::vector<int>{50, 75, 100, 150, 200, 300, 600, 1200, 2400}));
}

// Posledica kapije: dok H8 ne progovori, planer odbija SVE bez izricite
// dozvole - i to je ispravno ponasanje, ne greska.
TEST(CapabilityGate, PlannerRefusesEveryResolutionWithoutAllowUnqualified) {
    for (int dpi : executableResolutions()) {
        ScanRequest request;
        request.resolution = dpi;
        request.colorMode = ColorMode::Color;
        request.allowUnqualified = false;

        const auto plan = planScan(request);
        ASSERT_FALSE(plan) << dpi << " dpi je proslo bez hardverske potvrde";
        EXPECT_EQ(plan.error().code, ErrorCode::NotImplementedIn10) << dpi;
    }
}

// --- izbor reda i promena velicine -------------------------------------------

TEST(ScanPlanner, NativeResolutionsScanAtTheRequestedResolution) {
    for (int dpi : {150, 300, 600, 1200, 2400}) {
        const auto plan = planScan(flatbedRequest(dpi, ColorMode::Color));
        ASSERT_TRUE(plan) << dpi;
        EXPECT_EQ(plan.value().nativeResolution, dpi);
        EXPECT_EQ(plan.value().resize, ResizeType::None);
    }
}

// Scanmode_fitres bira najmanju native rezoluciju koja NIJE manja od trazene.
TEST(ScanPlanner, NonNativeResolutionsScanHigherAndShrink) {
    const struct {
        int requested;
        int expectedNative;
    } cases[] = {{50, 150}, {75, 150}, {100, 150}, {200, 300}};

    for (const auto& testCase : cases) {
        const auto plan = planScan(flatbedRequest(testCase.requested, ColorMode::Color));
        ASSERT_TRUE(plan) << testCase.requested;
        EXPECT_EQ(plan.value().nativeResolution, testCase.expectedNative)
            << testCase.requested << " dpi";
        EXPECT_EQ(plan.value().resize, ResizeType::Decrease);
        EXPECT_EQ(plan.value().requestedResolution, testCase.requested);
    }
}

TEST(ScanPlanner, EveryCombinationOfResolutionModeAndDepthPlans) {
    for (int dpi : executableResolutions()) {
        for (ColorMode mode : {ColorMode::Color, ColorMode::Gray, ColorMode::Lineart}) {
            for (int depth : {8, 16}) {
                const auto plan = planScan(flatbedRequest(dpi, mode, depth));
                ASSERT_TRUE(plan) << dpi << " " << toString(mode) << " " << depth;

                const auto& value = plan.value();
                EXPECT_GE(value.scanModeIndex, 0);
                EXPECT_EQ(value.scanMode().resolution, value.nativeResolution);
                EXPECT_EQ(value.scanMode().colorMode, static_cast<int>(value.tableMode));
                EXPECT_EQ(value.scanMode().scanType, kFlatbedScanType);
                EXPECT_GT(value.hardwareLine.bytesPerLine, 0u);
                EXPECT_GT(value.outputLine.bytesPerLine, 0u);
            }
        }
    }
}

TEST(ScanPlanner, CopiesTimingAndMotorFieldsFromTheChosenRow) {
    const auto plan = planScan(flatbedRequest(300, ColorMode::Color));
    ASSERT_TRUE(plan);
    const auto& value = plan.value();
    const auto& row = value.scanMode();

    EXPECT_EQ(value.timingIndex, row.timing);
    EXPECT_EQ(value.motorCurveIndex, row.motorCurve);
    EXPECT_EQ(value.systemClock, row.systemClock);
    EXPECT_EQ(value.ctpc, row.ctpc);
    EXPECT_EQ(value.motorBackStep, row.motorBackStep);
    EXPECT_GE(value.motorCurveIndex, 0) << "300 dpi u boji mora imati motornu krivu";
}

// 1200 i 2400 nemaju motornu krivu u tabeli (-1). Nije greska ekstrakcije -
// tako stoji i u referenci, i deo je razloga zasto su te dve iskljucene.
TEST(ScanPlanner, HighResolutionRowsHaveNoMotorCurve) {
    for (int dpi : {1200, 2400}) {
        const auto plan = planScan(flatbedRequest(dpi, ColorMode::Color));
        ASSERT_TRUE(plan) << dpi;
        EXPECT_EQ(plan.value().motorCurveIndex, -1) << dpi;
    }
}

TEST(ScanPlanner, UsbOneSpeedSelectsADifferentRow) {
    ScanRequest request = flatbedRequest(300, ColorMode::Color);
    request.usb = UsbSpeed::Usb11;

    const auto plan = planScan(request);
    ASSERT_TRUE(plan);
    EXPECT_EQ(profile::kScanModes[plan.value().scanModeIndex].usb, 0);

    const auto fast = planScan(flatbedRequest(300, ColorMode::Color));
    ASSERT_TRUE(fast);
    EXPECT_NE(plan.value().scanModeIndex, fast.value().scanModeIndex);
}

// --- lineart ------------------------------------------------------------------

TEST(ScanPlanner, LineartUsesGrayRowsAndEightBitHardwareDepth) {
    const auto plan = planScan(flatbedRequest(300, ColorMode::Lineart, 16));
    ASSERT_TRUE(plan);
    const auto& value = plan.value();

    EXPECT_EQ(value.tableMode, ColorMode::Gray);
    EXPECT_EQ(value.hardwareDepth, 8) << "referenca gasi dubinu za lineart";
    EXPECT_EQ(value.scanMode().colorMode, static_cast<int>(ColorMode::Gray));

    // Hardver isporucuje bajt po tacki; izlaz je bit po tacki.
    EXPECT_EQ(value.hardwareLine.bytesPerLine,
              static_cast<std::size_t>(value.nativeRegion.width));
    EXPECT_EQ(value.outputLine.bytesPerLine,
              static_cast<std::size_t>(value.requestedRegion.width + 7) / 8);
}

TEST(ScanPlanner, ColorAndGrayLineLengthsFollowChannelCountAndDepth) {
    const auto color = planScan(flatbedRequest(300, ColorMode::Color, 16));
    ASSERT_TRUE(color);
    EXPECT_EQ(color.value().hardwareLine.bytesPerLine,
              static_cast<std::size_t>(color.value().nativeRegion.width) * 3 * 2);

    const auto gray = planScan(flatbedRequest(300, ColorMode::Gray, 8));
    ASSERT_TRUE(gray);
    EXPECT_EQ(gray.value().hardwareLine.bytesPerLine,
              static_cast<std::size_t>(gray.value().nativeRegion.width));
}

// Veran port jedne grane koja se na ovom uredjaju nikada ne izvrsava.
TEST(ScanPlanner, GrayPixelRateDoublingNeverTriggersOnThisDevice) {
    for (const auto& row : profile::kScanModes) {
        if (row.mode.scanType == kFlatbedScanType && row.mode.colorMode == 1) {
            EXPECT_EQ(row.mode.sampleRate, 1) << "sivi red na PIXEL_RATE";
        }
    }
    for (int dpi : executableResolutions()) {
        const auto plan = planScan(flatbedRequest(dpi, ColorMode::Gray));
        ASSERT_TRUE(plan) << dpi;
        EXPECT_FALSE(plan.value().grayPixelRateDoubling) << dpi;
    }
}

// --- oblast -------------------------------------------------------------------

TEST(ScanPlanner, EmptyRegionMeansTheWholeSurface) {
    const auto plan = planScan(flatbedRequest(300, ColorMode::Color));
    ASSERT_TRUE(plan);
    const ScanRegion full = fullFlatbedRegion(300);

    EXPECT_EQ(plan.value().requestedRegion.width, full.width);
    EXPECT_EQ(plan.value().requestedRegion.height, full.height);
    EXPECT_GT(full.width, 0);
    EXPECT_GT(full.height, 0);
}

TEST(ScanPlanner, SurfaceScalesWithResolution) {
    const ScanRegion at150 = fullFlatbedRegion(150);
    const ScanRegion at300 = fullFlatbedRegion(300);
    EXPECT_NEAR(static_cast<double>(at300.width) / at150.width, 2.0, 0.01);
    EXPECT_NEAR(static_cast<double>(at300.height) / at150.height, 2.0, 0.01);
}

TEST(ScanPlanner, RegionIsScaledToTheNativeResolution) {
    ScanRequest request = flatbedRequest(100, ColorMode::Color);
    request.region = {10, 20, 200, 300};

    const auto plan = planScan(request);
    ASSERT_TRUE(plan);
    const auto& value = plan.value();

    ASSERT_EQ(value.nativeResolution, 150);
    EXPECT_EQ(value.nativeRegion.left, 15);
    EXPECT_EQ(value.nativeRegion.top, 30);
    EXPECT_EQ(value.nativeRegion.width, 300);

    // rts8822.c:1717 dodaje dva reda visine da smanjivanju ne ponestane ulaza.
    EXPECT_EQ(value.nativeRegion.height, 450 + 2);

    // Izlazna geometrija ostaje na trazenoj rezoluciji.
    EXPECT_EQ(value.requestedRegion.width, 200);
    EXPECT_EQ(value.outputLine.bytesPerLine, 200u * 3);
}

TEST(ScanPlanner, NativeResolutionLeavesTheRegionUntouched) {
    ScanRequest request = flatbedRequest(300, ColorMode::Color);
    request.region = {10, 20, 200, 300};

    const auto plan = planScan(request);
    ASSERT_TRUE(plan);
    const auto& value = plan.value();

    EXPECT_EQ(value.nativeRegion.left, 10);
    EXPECT_EQ(value.nativeRegion.top, 20);
    EXPECT_EQ(value.nativeRegion.width, 200);
    EXPECT_EQ(value.nativeRegion.height, 300) << "bez promene velicine nema +2";
}

TEST(ScanPlanner, RegionOutsideTheSurfaceIsRejected) {
    const ScanRegion full = fullFlatbedRegion(300);

    const struct {
        ScanRegion region;
        const char* what;
    } cases[] = {
        {{-1, 0, 100, 100}, "levo od nule"},
        {{0, -1, 100, 100}, "iznad nule"},
        {{full.width - 10, 0, 100, 100}, "preko desne ivice"},
        {{0, full.height - 10, 100, 100}, "preko donje ivice"},
    };

    for (const auto& testCase : cases) {
        ScanRequest request = flatbedRequest(300, ColorMode::Color);
        request.region = testCase.region;

        const auto plan = planScan(request);
        ASSERT_FALSE(plan) << testCase.what;
        EXPECT_EQ(plan.error().code, ErrorCode::InvalidArgument) << testCase.what;
    }
}

TEST(ScanPlanner, RegionFlushWithTheEdgeIsAccepted) {
    const ScanRegion full = fullFlatbedRegion(300);

    ScanRequest request = flatbedRequest(300, ColorMode::Color);
    request.region = {full.width - 100, full.height - 100, 100, 100};

    EXPECT_TRUE(planScan(request));
}

// --- poravnanje kanala --------------------------------------------------------

// Ovde se vidi D3: granica pada tacno izmedju 600 i 1200.
TEST(ScanPlanner, HardwareAlignmentStopsAboveSixHundred) {
    for (int dpi : {50, 75, 100, 150, 200, 300, 600}) {
        const auto plan = planScan(flatbedRequest(dpi, ColorMode::Color));
        ASSERT_TRUE(plan) << dpi;
        EXPECT_TRUE(plan.value().useHardwareAlignment) << dpi << " dpi";
        EXPECT_EQ(plan.value().alignmentLookahead, 0) << dpi;
    }

    for (int dpi : {1200, 2400}) {
        const auto plan = planScan(flatbedRequest(dpi, ColorMode::Color));
        ASSERT_TRUE(plan) << dpi;
        EXPECT_FALSE(plan.value().useHardwareAlignment) << dpi << " dpi";
        EXPECT_GT(plan.value().softwareLineDistance, 0) << dpi;
        EXPECT_EQ(plan.value().alignmentLookahead,
                  plan.value().softwareLineDistance * 2)
            << dpi;
    }
}

TEST(ScanPlanner, AlignmentIsComputedAtTheNativeResolutionNotTheRequestedOne) {
    // 100 dpi se skenira na 150; pomak mora odgovarati stotinu pedeset.
    const auto plan = planScan(flatbedRequest(100, ColorMode::Color));
    ASSERT_TRUE(plan);
    // 64 * 150 / 2400 = 4
    EXPECT_EQ(plan.value().lineOffsets.lineDistance, 4);
}

TEST(ScanPlanner, GrayAndLineartNeedNoChannelAlignment) {
    for (ColorMode mode : {ColorMode::Gray, ColorMode::Lineart}) {
        for (int dpi : {600, 2400}) {
            const auto plan = planScan(flatbedRequest(dpi, mode));
            ASSERT_TRUE(plan) << dpi;
            EXPECT_FALSE(plan.value().useHardwareAlignment) << toString(mode) << " " << dpi;
            EXPECT_EQ(plan.value().alignmentLookahead, 0) << toString(mode) << " " << dpi;
        }
    }
}

// --- odbijanja ----------------------------------------------------------------

TEST(ScanPlanner, TmaAndNegativeAreOutOfScopeForTenPointZero) {
    for (ScanSource source : {ScanSource::Tma, ScanSource::Negative}) {
        ScanRequest request = flatbedRequest(300, ColorMode::Color);
        request.source = source;

        const auto plan = planScan(request);
        ASSERT_FALSE(plan) << toString(source);
        EXPECT_EQ(plan.error().code, ErrorCode::NotImplementedIn10) << toString(source);
    }
}

TEST(ScanPlanner, UnknownResolutionIsRejected) {
    for (int dpi : {0, -300, 400, 1000, 4800}) {
        const auto plan = planScan(flatbedRequest(dpi, ColorMode::Color));
        ASSERT_FALSE(plan) << dpi;
        EXPECT_EQ(plan.error().code, ErrorCode::InvalidArgument) << dpi;
    }
}

TEST(ScanPlanner, OnlyEightAndSixteenBitDepthsAreAccepted) {
    for (int depth : {1, 4, 12, 24, 48}) {
        const auto plan = planScan(flatbedRequest(300, ColorMode::Color, depth));
        ASSERT_FALSE(plan) << depth;
        EXPECT_EQ(plan.error().code, ErrorCode::InvalidArgument) << depth;
    }
}
