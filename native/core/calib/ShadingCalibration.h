// Shading kalibracija: po-pikselni offset i pojacanje.
//
// Senzor ne odgovara isto na svakom pikselu. Referenca to resava u dva koraka
// koja se u kodu zovu black i white shading:
//
//   crna traka, lampa relevantna -> tamna struja po pikselu   (offset)
//   bela traka                   -> odziv po pikselu          (pojacanje)
//
// Ispravka je onda `(sirovo - offset) * gain`.
//
// Ovo je CIST racun nad izmerenim redovima - ne dodiruje uredjaj. Ko cita
// redove odlucuje pozivalac, pa se ceo modul testira bez transporta.

#pragma once

#include "../util/Result.h"
#include "CalibrationConfig.h"

#include <cstdint>
#include <functional>
#include <span>
#include <vector>

namespace g2710::calib {

// Koeficijenti za jedan izvor i jednu rezoluciju.
struct ShadingCoefficients {
    std::size_t pixelsPerLine = 0;

    // Po kanalu pa po pikselu: kChannels * pixelsPerLine.
    std::vector<double> darkOffset;
    std::vector<double> gain;

    double offsetAt(std::size_t channel, std::size_t pixel) const {
        return darkOffset[channel * pixelsPerLine + pixel];
    }
    double gainAt(std::size_t channel, std::size_t pixel) const {
        return gain[channel * pixelsPerLine + pixel];
    }

    bool empty() const noexcept { return pixelsPerLine == 0; }
};

// Koja se traka meri.
//
// Citac MORA znati ovo. Dve trake su na razlicitim mestima i mere se pod
// razlicitim uslovima - crna se cita bez odziva lampe. Citac koji to mora da
// pogadja iz rednog broja poziva grei cim se visine dve trake razlikuju, a
// za G2710 se razlikuju: 20 redova crne, 24 bele.
enum class ShadingStrip {
    Black,
    White,
};

const char* toString(ShadingStrip strip) noexcept;

// Cita jedan red senzora. `line` je redni broj unutar trake.
using LineReader = std::function<Status(ShadingStrip strip, std::size_t channel,
                                        int line, std::span<std::uint16_t> out)>;

struct ShadingMeasurement {
    // Koliko je redova stvarno usrednjeno po traci.
    int darkLines = 0;
    int whiteLines = 0;
};

// Granice koje koeficijent mora zadovoljiti da bi bio prihvacen.
//
// Bez ovoga bi pokvarena kalibracija - lampa koja se nije upalila, traka koja
// nije ispod glave - proizvela koeficijente koji sliku unistavaju umesto da je
// poprave, i to tiho.
struct ShadingLimits {
    double minGain = 0.2;
    double maxGain = 8.0;

    // Najmanja razlika bele i crne trake ispod koje merenje nema smisla.
    double minDynamicRange = 500.0;

    // Najveci udeo piksela koji sme ispasti van granica pre nego sto se ceo
    // set odbaci.
    double maxOutlierFraction = 0.02;
};

enum class ShadingRejection {
    None,
    NoDynamicRange,   // bela i crna se ne razlikuju - lampa nije upaljena?
    TooManyOutliers,  // previse piksela van granica
    EmptyMeasurement,
};

const char* toString(ShadingRejection reason) noexcept;

struct ShadingValidation {
    ShadingRejection reason = ShadingRejection::None;
    int outliers = 0;
    double worstGain = 0.0;
    double medianDynamicRange = 0.0;

    bool accepted() const noexcept { return reason == ShadingRejection::None; }
};

class ShadingCalibration {
public:
    explicit ShadingCalibration(ShadingLimits limits = {}) : limits_(limits) {}

    // Izmeri crnu pa belu traku i izracunaj koeficijente.
    //
    // `pixelsPerLine` mora odgovarati redovima koje `reader` puni.
    //
    // `sensorBitDepth` je dubina u kojoj citac vraca vrednosti. Bele reference
    // iz profila su izrazene na CalibrationConfig::referenceBitDepth (8 bita
    // za G2710), pa se moraju skalirati - inace bi pojacanje ispalo oko 0.008
    // umesto oko 1.0. To je jedina svrha polja RefBitDepth u referenci.
    Result<ShadingCoefficients> measure(const CalibrationConfig& config,
                                        std::size_t pixelsPerLine,
                                        const LineReader& reader,
                                        int sensorBitDepth = 16,
                                        ShadingMeasurement* stats = nullptr);

    // Ciljna vrednost bele, skalirana sa referentne na senzorsku dubinu.
    static double scaledWhiteTarget(const CalibrationConfig& config,
                                    std::size_t channel, int sensorBitDepth) noexcept;

    // Proveri da li se koeficijenti smeju upotrebiti.
    ShadingValidation validate(const ShadingCoefficients& coefficients) const;

    // Primeni ispravku na jedan sirovi red.
    static void apply(const ShadingCoefficients& coefficients, std::size_t channel,
                      std::span<std::uint16_t> line);

    const ShadingLimits& limits() const noexcept { return limits_; }

private:
    ShadingLimits limits_;
};

}  // namespace g2710::calib
