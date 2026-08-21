// Sta WIA nudi Windowsu i sta prihvata nazad.
//
// Prva grupa drzi zakljucanim pravilo iz MASTER plana: WIA oglasava iskljucivo
// hardverski potvrdjeno. Dok H faza ne progovori, izdanje ne nudi NISTA - i to
// nije kvar nego namera. Test pada cim neko to omeksa.
//
// Druga grupa proverava ono sto WIA od drajvera OCEKUJE: da ispravi sto se
// moze umesto da odbije. Aplikacija koja trazi oblast vecu od stakla nije
// pogresila - samo ne zna granicu.

#include "WiaCapabilities.h"

// WiaDef.h trazi dve stvari pre sebe: _WIN32_WINNT (iz windows.h) i tip
// WIA_PROPID_TO_NAME (iz wia.h). Bez njih se ne otvara, pa ovaj redosled nije
// stvar ukusa.
#include <windows.h>

#include <wia.h>

#include <WiaDef.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <vector>

using namespace g2710;
using namespace g2710::wia;

namespace {

bool offers(int dpi) {
    const std::vector<int> offered = offeredResolutions();
    return std::find(offered.begin(), offered.end(), dpi) != offered.end();
}

}  // namespace

// --- vrednosti moraju odgovarati SDK-u ---------------------------------------------

// Konstante su prepisane u nas enum da bi se sloj odluka testirao bez COM-a.
// Ovaj test je cena tog prepisa.
TEST(WiaTypes, MatchTheSdk) {
    EXPECT_EQ(static_cast<int>(WiaDataType::Threshold), WIA_DATA_THRESHOLD);
    EXPECT_EQ(static_cast<int>(WiaDataType::Grayscale), WIA_DATA_GRAYSCALE);
    EXPECT_EQ(static_cast<int>(WiaDataType::Color), WIA_DATA_COLOR);
}

TEST(WiaTypes, MapToAndFromOurColorModes) {
    for (WiaDataType type : offeredDataTypes()) {
        EXPECT_EQ(toWiaDataType(toColorMode(type)), type) << toString(type);
    }
}

// WIA_IPA_DEPTH je bita po TACKI, ne po kanalu - lako se pomesa.
TEST(WiaTypes, DepthIsPerDotNotPerChannel) {
    EXPECT_EQ(wiaDepthFor(image::ColorMode::Color, 8), 24);
    EXPECT_EQ(wiaDepthFor(image::ColorMode::Color, 16), 48);
    EXPECT_EQ(wiaDepthFor(image::ColorMode::Gray, 8), 8);
    EXPECT_EQ(wiaDepthFor(image::ColorMode::Gray, 16), 16);
    EXPECT_EQ(wiaDepthFor(image::ColorMode::Lineart, 8), 1);

    EXPECT_EQ(bitsPerChannelFrom(image::ColorMode::Color, 24), 8);
    EXPECT_EQ(bitsPerChannelFrom(image::ColorMode::Color, 48), 16);
    EXPECT_EQ(bitsPerChannelFrom(image::ColorMode::Gray, 16), 16);
    EXPECT_EQ(bitsPerChannelFrom(image::ColorMode::Lineart, 1), 8)
        << "lineart se skenira kao osmobitno sivo pa se pragom svodi na bit";
}

// --- kapija: sta se uopste nudi -------------------------------------------------------

// Ovaj test pada cim neko oznaci bilo sta kao HARDWARE_VALIDATED bez pratecih
// H rezultata, ili omeksa pravilo u offeredResolutions().
TEST(WiaGate, ReleaseBuildOffersNothingBeforeHardwareQualification) {
    if (kAllowUnqualified) {
        GTEST_SKIP() << "kvalifikacioni build - vidi WiaGate.QualificationBuild";
    }

    EXPECT_TRUE(scan::advertisableResolutions().empty())
        << "ako ovo vise nije prazno, H rezultati moraju biti u repozitorijumu";
    EXPECT_TRUE(offeredResolutions().empty());
    EXPECT_FALSE(hasUsableConfiguration());
    EXPECT_EQ(defaultResolution(), 0);
}

// Prazna ponuda nije prazna slika - drajver mora imati odgovor. Svaki poziv
// koji trazi vrednosti mora ODBITI, a ne vratiti nesto nasumicno.
TEST(WiaGate, WithNothingOfferedEveryRequestIsRefused) {
    if (kAllowUnqualified) {
        GTEST_SKIP() << "kvalifikacioni build nudi rezolucije";
    }

    ItemSettings settings;
    settings.xResolution = 300;
    settings.yResolution = 300;

    const Validation checked = validate(settings);
    EXPECT_FALSE(checked.accepted());
    EXPECT_EQ(checked.reason, Rejection::NoUsableConfiguration);

    EXPECT_FALSE(toScanRequest(settings));
    EXPECT_FALSE(imageSizeInBytes(settings));
}

// Kvalifikacioni build nudi ono sto je poklopljeno sa referencom, ali NE i
// 1200/2400 - te dve nose D3 i otvara ih tek H8.
TEST(WiaGate, QualificationBuildOffersReferenceValidatedOnly) {
    if (!kAllowUnqualified) {
        GTEST_SKIP() << "izdanje - vidi WiaGate.ReleaseBuildOffersNothing";
    }

    EXPECT_TRUE(hasUsableConfiguration());
    EXPECT_TRUE(offers(300));
    EXPECT_FALSE(offers(1200)) << "D3 ceka H8";
    EXPECT_FALSE(offers(2400)) << "D3 ceka H8";
}

// --- ponuda ---------------------------------------------------------------------------

TEST(WiaOffer, DataTypesCoverAllThreeModes) {
    const auto types = offeredDataTypes();
    ASSERT_EQ(types.size(), 3u);
    EXPECT_NE(std::find(types.begin(), types.end(), WiaDataType::Color), types.end());
    EXPECT_NE(std::find(types.begin(), types.end(), WiaDataType::Grayscale), types.end());
    EXPECT_NE(std::find(types.begin(), types.end(), WiaDataType::Threshold), types.end());
}

TEST(WiaOffer, DepthsFollowTheDataType) {
    EXPECT_EQ(offeredBitDepths(WiaDataType::Color).size(), 2u);
    EXPECT_EQ(offeredBitDepths(WiaDataType::Grayscale).size(), 2u);

    const auto lineart = offeredBitDepths(WiaDataType::Threshold);
    ASSERT_EQ(lineart.size(), 1u);
    EXPECT_EQ(lineart[0], 1) << "lineart ima tacno jednu dubinu";
}

TEST(WiaOffer, SurfaceComesFromTheProfile) {
    const SurfaceMillimetres surface = flatbedSurface();
    EXPECT_EQ(surface.width, 220);
    EXPECT_EQ(surface.height, 300);
}

TEST(WiaOffer, OfferedResolutionsAreAllExecutable) {
    const std::vector<int> executable = scan::executableResolutions();
    for (int dpi : offeredResolutions()) {
        EXPECT_NE(std::find(executable.begin(), executable.end(), dpi), executable.end())
            << dpi << " se nudi a kod ga ne ume izvrsiti";
    }
}

// --- provera i ispravka -----------------------------------------------------------------

class WiaValidation : public ::testing::Test {
protected:
    void SetUp() override {
        if (!hasUsableConfiguration()) {
            GTEST_SKIP() << "izdanje ne nudi nijednu rezoluciju - vidi WiaGate";
        }
    }

    ItemSettings good() const { return defaultSettings(); }
};

TEST_F(WiaValidation, DefaultsAreAccepted) {
    const ItemSettings settings = good();
    EXPECT_GT(settings.xResolution, 0);
    EXPECT_EQ(settings.xResolution, settings.yResolution);
    EXPECT_GT(settings.xExtent, 0);
    EXPECT_GT(settings.yExtent, 0);

    const Validation checked = validate(settings);
    EXPECT_TRUE(checked.accepted()) << toString(checked.reason);
    EXPECT_FALSE(checked.changed);
}

// WIA dozvoljava razlicite X i Y rezolucije; nas hardver ne. Drajver to
// ISPRAVLJA - odbijanje bi aplikaciju ostavilo bez objasnjenja.
TEST_F(WiaValidation, DifferingResolutionsAreEqualisedNotRefused) {
    ItemSettings settings = good();
    const int wanted = settings.xResolution;
    settings.yResolution = wanted * 2;

    const Validation checked = validate(settings);
    EXPECT_TRUE(checked.accepted()) << toString(checked.reason);
    EXPECT_TRUE(checked.changed);
    EXPECT_EQ(checked.corrected.yResolution, checked.corrected.xResolution);

    // X POBEDJUJE. Aplikacije postavljaju X pa Y, pa je X ono sto je korisnik
    // izabrao; uzeti Y znacilo bi tiho skenirati na drugoj rezoluciji.
    EXPECT_EQ(checked.corrected.xResolution, wanted) << "X mora ostati merodavan";
}

TEST_F(WiaValidation, UnofferedResolutionIsRefused) {
    ItemSettings settings = good();
    settings.xResolution = 137;
    settings.yResolution = 137;

    const Validation checked = validate(settings);
    EXPECT_FALSE(checked.accepted());
    EXPECT_EQ(checked.reason, Rejection::ResolutionNotOffered);
}

TEST_F(WiaValidation, WrongDepthIsRefusedForColorAndFixedForLineart) {
    ItemSettings color = good();
    color.dataType = WiaDataType::Color;
    color.wiaDepth = 12;
    EXPECT_EQ(validate(color).reason, Rejection::DepthNotOffered);

    ItemSettings lineart = good();
    lineart.dataType = WiaDataType::Threshold;
    lineart.wiaDepth = 8;

    const Validation checked = validate(lineart);
    EXPECT_TRUE(checked.accepted());
    EXPECT_TRUE(checked.changed);
    EXPECT_EQ(checked.corrected.wiaDepth, 1);
}

// Aplikacija koja trazi vise nego sto staklo ima nije pogresila - samo ne zna
// granicu. Zato se SECE, ne odbija.
TEST_F(WiaValidation, OversizedRegionIsClippedToTheSurface) {
    ItemSettings settings = good();
    const scan::ScanRegion surface = scan::fullFlatbedRegion(settings.xResolution);

    settings.xPosition = 10;
    settings.yPosition = 20;
    settings.xExtent = surface.width * 2;
    settings.yExtent = surface.height * 2;

    const Validation checked = validate(settings);
    ASSERT_TRUE(checked.accepted()) << toString(checked.reason);
    EXPECT_TRUE(checked.changed);
    EXPECT_EQ(checked.corrected.xExtent, surface.width - 10);
    EXPECT_EQ(checked.corrected.yExtent, surface.height - 20);
}

TEST_F(WiaValidation, NegativePositionIsMovedToZero) {
    ItemSettings settings = good();
    settings.xPosition = -5;
    settings.yPosition = -7;

    const Validation checked = validate(settings);
    ASSERT_TRUE(checked.accepted());
    EXPECT_EQ(checked.corrected.xPosition, 0);
    EXPECT_EQ(checked.corrected.yPosition, 0);
}

TEST_F(WiaValidation, PositionBeyondTheSurfaceIsRefused) {
    ItemSettings settings = good();
    const scan::ScanRegion surface = scan::fullFlatbedRegion(settings.xResolution);
    settings.xPosition = surface.width;

    const Validation checked = validate(settings);
    EXPECT_FALSE(checked.accepted());
    EXPECT_EQ(checked.reason, Rejection::RegionOutsideSurface);
}

TEST_F(WiaValidation, ZeroExtentMeansTheWholeSurface) {
    ItemSettings settings = good();
    settings.xExtent = 0;
    settings.yExtent = 0;

    const Validation checked = validate(settings);
    ASSERT_TRUE(checked.accepted());
    EXPECT_TRUE(checked.changed);

    const scan::ScanRegion surface = scan::fullFlatbedRegion(settings.xResolution);
    EXPECT_EQ(checked.corrected.xExtent, surface.width);
    EXPECT_EQ(checked.corrected.yExtent, surface.height);
}

// --- prevod u zahtev za skeniranje --------------------------------------------------------

TEST_F(WiaValidation, TranslatesIntoAScanRequest) {
    ItemSettings settings = good();
    settings.dataType = WiaDataType::Grayscale;
    settings.wiaDepth = 16;
    settings.xPosition = 4;
    settings.yPosition = 6;
    settings.xExtent = 100;
    settings.yExtent = 200;

    const auto request = toScanRequest(settings);
    ASSERT_TRUE(request) << "prevod je pao";

    EXPECT_EQ(request.value().resolution, settings.xResolution);
    EXPECT_EQ(request.value().colorMode, image::ColorMode::Gray);
    EXPECT_EQ(request.value().depth, 16);
    EXPECT_EQ(request.value().source, scan::ScanSource::Flatbed);
    EXPECT_EQ(request.value().region.left, 4);
    EXPECT_EQ(request.value().region.width, 100);
    EXPECT_EQ(request.value().region.height, 200);
    EXPECT_EQ(request.value().allowUnqualified, kAllowUnqualified);
}

// Prevedeni zahtev mora zaista proci kroz planer. Ako ne prodje, WIA bi
// prihvatio vrednosti pa pao tek pri skeniranju.
TEST_F(WiaValidation, EveryOfferedCombinationSurvivesThePlanner) {
    for (int dpi : offeredResolutions()) {
        for (WiaDataType type : offeredDataTypes()) {
            for (int depth : offeredBitDepths(type)) {
                ItemSettings settings = good();
                settings.xResolution = dpi;
                settings.yResolution = dpi;
                settings.dataType = type;
                settings.wiaDepth = depth;
                settings.xExtent = 0;
                settings.yExtent = 0;

                const auto request = toScanRequest(settings);
                ASSERT_TRUE(request) << dpi << " " << toString(type) << " " << depth;

                const auto plan = scan::planScan(request.value());
                EXPECT_TRUE(plan) << dpi << " " << toString(type) << " " << depth
                                  << ": " << (plan ? "" : plan.error().context);
            }
        }
    }
}

TEST_F(WiaValidation, ImageSizeMatchesTheLineGeometry) {
    ItemSettings settings = good();
    settings.dataType = WiaDataType::Color;
    settings.wiaDepth = 24;
    settings.xExtent = 100;
    settings.yExtent = 50;

    const auto size = imageSizeInBytes(settings);
    ASSERT_TRUE(size);
    EXPECT_EQ(size.value(), 100u * 3u * 50u);

    settings.dataType = WiaDataType::Threshold;
    settings.wiaDepth = 1;
    const auto lineart = imageSizeInBytes(settings);
    ASSERT_TRUE(lineart);
    EXPECT_EQ(lineart.value(), ((100u + 7u) / 8u) * 50u);
}

TEST_F(WiaValidation, ImageSizeIsRefusedForInvalidSettings) {
    ItemSettings settings = good();
    settings.xResolution = 137;
    settings.yResolution = 137;
    EXPECT_FALSE(imageSizeInBytes(settings));
}
