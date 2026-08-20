// Verifikuje G2710Profile.generated.h protiv nalaza iz docs/G2710-PROFILE.md
// i docs/PROTOCOL-RTS8822.md.
//
// Sve provere su compile-time: ako se ovaj TU kompajlira, profil je saglasan
// sa dokumentacijom. Regeneracija koja bi tiho promenila vrednost obara build.

#include "G2710Profile.generated.h"

#include <cstddef>

namespace {

using namespace g2710::profile;

template <typename T, std::size_t N>
constexpr std::size_t countOf(const T (&)[N]) {
    return N;
}

// ---------------------------------------------------------------- identitet
static_assert(kUsbVendorId == 0x03F0, "G2710 VID");
static_assert(kUsbProductId == 0x2805, "G2710 PID");
static_assert(kDeviceModelId == 7, "HPG2710");
static_assert(kChipsetModelId == 2, "RTS8822BL_03A");

// ---------------------------------------------------------------- transport
static_assert(kVendorRequest == 0x04, "bRequest");
static_assert(kBulkInEndpoint == 0x81, "bulk IN EP");
static_assert(kBulkOutEndpoint == 0x02, "bulk OUT EP");
static_assert(kRegisterBankBase == 0xE800, "register bank baza");
static_assert(kRegisterBankLength == 1818, "RT_BUFFER_LEN = 0x71a");

// wIndex je selektor komande; read i write MORAJU imati razlicite indekse.
static_assert(kIndexRegisterRead != kIndexRegisterWrite,
              "register read/write asimetrija je namerna");

// ------------------------------------------------------------------- senzor
// Ulaz za LineOffsetCorrector; izrazeno u jedinicama 2400 dpi.
static_assert(kSensor.resolution == 2400, "senzor 2400 dpi");
static_assert(kSensor.lineDistance == 64, "CCD RGB line distance");
static_assert(kSensor.evenOddDistance == 8, "CCD even/odd distance");
static_assert(kSensor.channelColor[0] == 0 && kSensor.channelColor[1] == 1 &&
                  kSensor.channelColor[2] == 2,
              "RGB redosled kanala");

// -------------------------------------------------------------------- motor
// Senzor je 2400 dpi ali motor samo 1200 - verovatan trag za problem sa
// 1200/2400 dpi koji je autor reference opisao kao neresen.
static_assert(kMotor.resolution == 1200, "motor 1200 dpi");
static_assert(kSensor.resolution > kMotor.resolution,
              "asimetrija senzor/motor je stvarna i mora ostati vidljiva");

// ------------------------------------------------------------------- dugmad
static_assert(kButtons.count == 3, "Scan / Copy / PDF");
static_assert(kButtons.mask[0] == 0x01 && kButtons.mask[1] == 0x02 &&
                  kButtons.mask[2] == 0x04,
              "maske dugmadi");

// --------------------------------------------------------------- geometrija
static_assert(kConstraints.reflective.width == 220, "flatbed sirina mm");
static_assert(kConstraints.reflective.height == 300, "flatbed visina mm");
static_assert(kAutoRef.offsetX == 88 && kAutoRef.offsetY == 624, "reference position");

// ----------------------------------------------------------- velicine tabela
static_assert(countOf(kScanModes) == 60, "scan modova");
static_assert(countOf(kTimings) == 20, "timing profila");
static_assert(countOf(kMotorCurves) == 12, "motornih krivih");
static_assert(countOf(kMotorMoves) == 2, "profila kretanja");
static_assert(countOf(kCalibReflective) == 80, "flatbed calib parametara");

// --------------------------------------------------------------- white refs
// Za flatbed referenca ignorise kWhiteRefs i vraca fiksne vrednosti.
static_assert(kWhiteRefReflective[0] == 248 && kWhiteRefReflective[1] == 250 &&
                  kWhiteRefReflective[2] == 248,
              "flatbed white refs su fiksni, ne iz tabele");

// ------------------------------------------- unakrsna provera: motorCurve indeks
// Svaki scan mode referise motornu krivu po indeksu, ili -1 (nema krive).
// Dekodiranje motornih krivih iz flat streama je netrivijalno, pa ova provera
// stiti od tihog pomeranja indeksa pri regeneraciji.
constexpr bool motorCurveIndicesInRange() {
    const int count = static_cast<int>(countOf(kMotorCurves));
    for (const auto& row : kScanModes) {
        const int mc = row.mode.motorCurve;
        if (mc != -1 && (mc < 0 || mc >= count)) {
            return false;
        }
    }
    return true;
}
static_assert(motorCurveIndicesInRange(), "scan mode referise nepostojecu motornu krivu");

// -------------------------------------------- unakrsna provera: timing indeks
constexpr bool timingIndicesInRange() {
    const int count = static_cast<int>(countOf(kTimings));
    for (const auto& row : kScanModes) {
        if (row.mode.timing < 0 || row.mode.timing >= count) {
            return false;
        }
    }
    return true;
}
static_assert(timingIndicesInRange(), "scan mode referise nepostojeci timing profil");

// ------------------------------------ nijedna motorna kriva nije prazna/krnja
constexpr bool motorCurvesWellFormed() {
    for (const auto& curve : kMotorCurves) {
        for (const auto& seg : curve.segments) {
            if (seg.count <= 0 || seg.values == nullptr) {
                return false;
            }
        }
    }
    return true;
}
static_assert(motorCurvesWellFormed(), "motorna kriva ima prazan segment");

// ------------------------------------------------- native rezolucije modova
// 75 dpi NIJE native mod - najnizi je 100. Vidi docs/G2710-PROFILE.md 3d.
constexpr bool noNativeResolutionBelow100() {
    for (const auto& row : kScanModes) {
        if (row.mode.resolution < 100) {
            return false;
        }
    }
    return true;
}
static_assert(noNativeResolutionBelow100(), "75 dpi nije native mod, mora ici kroz resize");

// Lineart maksimum je 1200 cak i ako 2400 prodje kvalifikaciju.
constexpr bool lineartCapped1200() {
    constexpr int kLineart = 2;  // CM_LINEART
    for (const auto& row : kScanModes) {
        if (row.mode.colorMode == kLineart && row.mode.resolution > 1200) {
            return false;
        }
    }
    return true;
}
static_assert(lineartCapped1200(), "lineart nema native 2400 dpi mod");

}  // namespace

int main() {
    return 0;
}
