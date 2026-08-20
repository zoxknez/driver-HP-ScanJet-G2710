// Kalibracija protiv simulatora koji NAMERNO gresi.
//
// Ovo je acceptance gate faze G2710-5 iz plana:
//
//   "na simulatoru sa poznatom ubacenom senzorskom greskom, kalibracija je
//    ponistava do zadate tolerancije; CalibrationValidator odbija svaki
//    namerno korumpiran set"
//
// Test bez ubacene greske ne bi dokazao nista - prazna implementacija bi
// prolazila. Zato prvo merimo koliko greska iznosi, pa tek onda koliko je od
// nje ostalo.

#include "TestTarget.h"
#include "VirtualCcd.h"
#include "VirtualLamp.h"
#include "calib/CalibrationConfig.h"
#include "calib/ShadingCalibration.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <vector>

using namespace g2710;
using namespace g2710::calib;
using namespace g2710::sim;

namespace {

// Redovi mete na kojima su kalibracione trake, u koracima motora na 1200 dpi.
// Bela je 0-5 mm, crna 5-10 mm (TestTarget).
int rowForMillimetres(double mm) {
    return static_cast<int>((mm / 25.4) * 1200.0);
}

constexpr int kWhiteRow = 0;                 // 0 mm
const int kBlackRow = rowForMillimetres(7);  // sredina crne trake
const int kFieldRow = rowForMillimetres(150);  // neutralno sivo polje

VirtualLamp warmLamp() {
    VirtualLamp lamp;
    lamp.turnOn();
    lamp.advance(30000);
    return lamp;
}

double meanOf(const std::vector<std::uint16_t>& line) {
    double sum = 0.0;
    for (const auto value : line) {
        sum += value;
    }
    return line.empty() ? 0.0 : sum / static_cast<double>(line.size());
}

// Koeficijent varijacije: rasipanje u odnosu na srednju vrednost. Mera
// neujednacenosti koju shading treba da smanji.
double nonUniformity(const std::vector<std::uint16_t>& line) {
    const double mean = meanOf(line);
    if (mean <= 0.0) {
        return 0.0;
    }
    double variance = 0.0;
    for (const auto value : line) {
        const double delta = static_cast<double>(value) - mean;
        variance += delta * delta;
    }
    variance /= static_cast<double>(line.size());
    return std::sqrt(variance) / mean;
}

class Calibration : public ::testing::Test {
protected:
    VirtualCcd ccd;
    VirtualLamp lamp = warmLamp();
    VirtualLamp darkLamp;  // ugasena, za crnu traku

    std::size_t pixels() const { return ccd.pixelsPerLine(300); }

    // Citac zna koju traku cita - to je i realno, jer su dve trake na
    // razlicitim mestima i mere se pod razlicitim uslovima.
    LineReader stripReader() {
        return [this](ShadingStrip strip, std::size_t channel, int line,
                      std::span<std::uint16_t> out) -> Status {
            const bool dark = strip == ShadingStrip::Black;
            const int row = dark ? kBlackRow : kWhiteRow;
            const VirtualLamp& source = dark ? darkLamp : lamp;

            ccd.readLine(row + line, 300, static_cast<CcdChannel>(channel), source, out);
            return ok();
        };
    }

    std::vector<std::uint16_t> readField(std::size_t channel) {
        std::vector<std::uint16_t> line(pixels());
        ccd.readLine(kFieldRow, 300, static_cast<CcdChannel>(channel), lamp, line);
        return line;
    }

};

}  // namespace

// --- ucitavanje parametara ---------------------------------------------------

TEST(CalibrationConfigTest, ReflectiveSectionLoads) {
    const auto config = loadCalibrationConfig(CalibrationSection::Reflective);
    ASSERT_TRUE(config.hasValue());

    EXPECT_EQ(config.value().referenceBitDepth, 8);
    EXPECT_EQ(config.value().offsetHeight, 10);
    EXPECT_EQ(config.value().blackShadingHeight, 20);
    EXPECT_EQ(config.value().whiteShadingHeight, 24);
}

TEST(CalibrationConfigTest, PercentFieldsAreScaledByOneHundredth) {
    // rts8822.c:11517 - OFFSETTARGETMAX je 50 u tabeli, a koristi se kao 0.50.
    const auto config = loadCalibrationConfig(CalibrationSection::Reflective);
    ASSERT_TRUE(config.hasValue());

    EXPECT_DOUBLE_EQ(config.value().offsetTargetMax, 0.50);
    EXPECT_DOUBLE_EQ(config.value().offsetTargetMin, 0.02);
    EXPECT_DOUBLE_EQ(config.value().offsetBoundaryRatio1, 1.00);
    EXPECT_DOUBLE_EQ(config.value().offsetAvgRatio1, 1.00);
}

TEST(CalibrationConfigTest, WhiteReferenceComesFromTheFixedFlatbedValues) {
    // hp3800_wrefs za flatbed IGNORISE tabelu i vraca 248 / 250 / 248.
    const auto config = loadCalibrationConfig(CalibrationSection::Reflective);
    ASSERT_TRUE(config.hasValue());

    EXPECT_EQ(config.value().whiteReference[0], 248);
    EXPECT_EQ(config.value().whiteReference[1], 250);
    EXPECT_EQ(config.value().whiteReference[2], 248);
}

TEST(CalibrationConfigTest, ShadingFlagsAreTestedAsNonZeroNotPositive) {
    // BSHADINGON je -3 u profilu. Provera `> 0` bi ga iskljucila.
    ASSERT_EQ(calibrationValue(CalibrationSection::Reflective,
                               profile::CalibOption::BSHADINGON, 0),
              -3);

    const auto config = loadCalibrationConfig(CalibrationSection::Reflective);
    ASSERT_TRUE(config.hasValue());
    EXPECT_TRUE(config.value().blackShadingEnabled)
        << "negativna vrednost je protumacena kao iskljuceno";
}

TEST(CalibrationConfigTest, TmaSectionsAreRefusedNotSilentlySubstituted) {
    // TMA parametri postoje u profilu, ali nisu kvalifikovani i nose dva
    // poznata defekta. Tiho vracanje flatbed vrednosti bilo bi gore.
    for (const auto section :
         {CalibrationSection::Transparent, CalibrationSection::Negative}) {
        const auto config = loadCalibrationConfig(section);
        ASSERT_FALSE(config.hasValue()) << toString(section);
        EXPECT_EQ(config.error().code, ErrorCode::NotImplementedIn10);
    }
}

// --- GATE: kalibracija ponistava ubacenu gresku ------------------------------

TEST(CalibrationConfigTest, WhiteTargetIsScaledFromReferenceBitDepth) {
    // Bele reference su 8-bitne (248 / 250 / 248), a senzor daje 16 bita.
    // Bez skaliranja bi pojacanje ispalo oko 0.008 i validator bi odbio ceo
    // set - sto se i dogodilo pre nego sto je RefBitDepth pocelo da se koristi.
    const auto config = loadCalibrationConfig(CalibrationSection::Reflective);
    ASSERT_TRUE(config.hasValue());

    const double at8 = ShadingCalibration::scaledWhiteTarget(config.value(), 0, 8);
    const double at16 = ShadingCalibration::scaledWhiteTarget(config.value(), 0, 16);

    EXPECT_DOUBLE_EQ(at8, 248.0) << "na istoj dubini se ne skalira";
    EXPECT_NEAR(at16, 248.0 * 65535.0 / 255.0, 1.0);
    EXPECT_GT(at16, 60000.0);
}

TEST_F(Calibration, RawFieldIsMeasurablyNonUniformBeforeCalibration) {
    // Kontrola za test ispod. Ako bi sirovo polje vec bilo ravno, sledeci test
    // bi prolazio bez obzira na to sta kalibracija radi.
    const auto raw = readField(0);
    EXPECT_GT(nonUniformity(raw), 0.03)
        << "simulator ne pravi dovoljno greske da bi se kalibracija merila";
}

TEST_F(Calibration, ShadingCancelsTheInjectedSensorError) {
    // ACCEPTANCE GATE faze G2710-5.
    const auto config = loadCalibrationConfig(CalibrationSection::Reflective);
    ASSERT_TRUE(config.hasValue());

    ShadingCalibration calibration;
    const auto coefficients =
        calibration.measure(config.value(), pixels(), stripReader(), 16);
    ASSERT_TRUE(coefficients.hasValue());

    const auto validation = calibration.validate(coefficients.value());
    ASSERT_TRUE(validation.accepted()) << toString(validation.reason);

    for (std::size_t channel = 0; channel < kChannels; ++channel) {
        auto line = readField(channel);
        const double before = nonUniformity(line);

        ShadingCalibration::apply(coefficients.value(), channel, line);
        const double after = nonUniformity(line);

        EXPECT_LT(after, before * 0.25)
            << "kanal " << channel << ": neujednacenost je " << before << " -> " << after;
        EXPECT_LT(after, 0.02) << "kanal " << channel << ": rezidual iznad tolerancije";
    }
}

TEST_F(Calibration, EvenOddDifferenceIsCancelledToo) {
    // Parni i neparni pikseli idu kroz razdvojene ADC lance. Po-pikselni
    // koeficijenti to hvataju samo ako se stvarno racunaju po pikselu, a ne
    // kao jedna vrednost po kanalu.
    const auto config = loadCalibrationConfig(CalibrationSection::Reflective);
    ASSERT_TRUE(config.hasValue());

    ShadingCalibration calibration;
    const auto coefficients =
        calibration.measure(config.value(), pixels(), stripReader(), 16);
    ASSERT_TRUE(coefficients.hasValue());

    auto line = readField(0);

    const auto evenOddGap = [](const std::vector<std::uint16_t>& data) {
        double evenSum = 0.0, oddSum = 0.0;
        int evenCount = 0, oddCount = 0;
        for (std::size_t i = 0; i < data.size(); ++i) {
            if ((i & 1u) == 0) { evenSum += data[i]; ++evenCount; }
            else               { oddSum += data[i];  ++oddCount; }
        }
        return std::abs(evenSum / evenCount - oddSum / oddCount);
    };

    const double before = evenOddGap(line);
    ShadingCalibration::apply(coefficients.value(), 0, line);
    const double after = evenOddGap(line);

    EXPECT_GT(before, 50.0) << "simulator ne pravi even/odd razliku";
    EXPECT_LT(after, before * 0.2) << "even/odd razlika nije ponistena";
}

TEST_F(Calibration, IdealSensorNeedsNoCorrection) {
    // Kontrola u drugom smeru: nad savrsenim senzorom koeficijenti moraju
    // biti blizu jedinice, a ispravka ne sme pokvariti sliku.
    ccd.makeIdeal();

    const auto config = loadCalibrationConfig(CalibrationSection::Reflective);
    ASSERT_TRUE(config.hasValue());

    ShadingCalibration calibration;
    const auto coefficients =
        calibration.measure(config.value(), pixels(), stripReader(), 16);
    ASSERT_TRUE(coefficients.hasValue());

    auto line = readField(0);
    const double before = nonUniformity(line);
    ShadingCalibration::apply(coefficients.value(), 0, line);
    const double after = nonUniformity(line);

    EXPECT_LT(after, 0.02);
    EXPECT_LT(std::abs(after - before), 0.02) << "ispravka je pokvarila ravnu sliku";
}

// --- validator ---------------------------------------------------------------

TEST_F(Calibration, ValidatorRejectsMeasurementWithNoDynamicRange) {
    // Lampa se nije upalila: bela i crna traka daju isto. Koeficijenti bi
    // sliku unistili, i to tiho.
    lamp.turnOff();

    const auto config = loadCalibrationConfig(CalibrationSection::Reflective);
    ASSERT_TRUE(config.hasValue());

    ShadingCalibration calibration;
    const auto coefficients =
        calibration.measure(config.value(), pixels(), stripReader(), 16);
    ASSERT_TRUE(coefficients.hasValue());

    const auto validation = calibration.validate(coefficients.value());
    EXPECT_FALSE(validation.accepted());
    EXPECT_EQ(validation.reason, ShadingRejection::NoDynamicRange);
}

TEST_F(Calibration, ValidatorRejectsCorruptedCoefficients) {
    const auto config = loadCalibrationConfig(CalibrationSection::Reflective);
    ASSERT_TRUE(config.hasValue());

    ShadingCalibration calibration;
    auto coefficients = calibration.measure(config.value(), pixels(), stripReader(), 16);
    ASSERT_TRUE(coefficients.hasValue());
    ASSERT_TRUE(calibration.validate(coefficients.value()).accepted());

    // Pokvari desetinu piksela - daleko iznad dozvoljenih 2%.
    auto corrupted = coefficients.value();
    for (std::size_t i = 0; i < corrupted.gain.size(); i += 10) {
        corrupted.gain[i] = 1000.0;
    }

    const auto validation = calibration.validate(corrupted);
    EXPECT_FALSE(validation.accepted());
    EXPECT_EQ(validation.reason, ShadingRejection::TooManyOutliers);
    EXPECT_GT(validation.outliers, 0);
}

TEST_F(Calibration, ValidatorRejectsEmptySet) {
    ShadingCalibration calibration;
    const ShadingCoefficients empty;

    const auto validation = calibration.validate(empty);
    EXPECT_FALSE(validation.accepted());
    EXPECT_EQ(validation.reason, ShadingRejection::EmptyMeasurement);
}

TEST_F(Calibration, ValidatorToleratesAFewDeadPixels) {
    // Nekoliko mrtvih piksela je normalno i ne sme oboriti ceo set.
    const auto config = loadCalibrationConfig(CalibrationSection::Reflective);
    ASSERT_TRUE(config.hasValue());

    ShadingCalibration calibration;
    auto coefficients = calibration.measure(config.value(), pixels(), stripReader(), 16);
    ASSERT_TRUE(coefficients.hasValue());

    auto patched = coefficients.value();
    patched.gain[0] = 0.0;
    patched.gain[1] = 0.0;

    const auto validation = calibration.validate(patched);
    EXPECT_TRUE(validation.accepted()) << toString(validation.reason);
    EXPECT_EQ(validation.outliers, 2);
}

TEST_F(Calibration, MeasurementFailurePropagates) {
    const auto config = loadCalibrationConfig(CalibrationSection::Reflective);
    ASSERT_TRUE(config.hasValue());

    ShadingCalibration calibration;
    const auto result = calibration.measure(
        config.value(), pixels(),
        [](ShadingStrip, std::size_t, int, std::span<std::uint16_t>) -> Status {
            return fail(ErrorCode::TransportLost, "citanje trake");
        });

    ASSERT_FALSE(result.hasValue());
    EXPECT_EQ(result.error().code, ErrorCode::TransportLost);
}
