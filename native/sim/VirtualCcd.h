// Virtuelni CCD.
//
// Ovo nije "izvor piksela". To je deo simulatora koji NAMERNO gresi, jer bi
// savrsen senzor ucinio ceo kalibracioni podsistem iz G2710-5 neproverljivim:
// shading, ADC gain i offset ne bi imali sta da poniste, pa bi i prazna
// implementacija prolazila testove.
//
// Modelirane greske su one koje referenca stvarno kompenzuje:
//
//   po-pikselno pojacanje    -> white shading
//   tamna struja po pikselu  -> black shading
//   razlika parnih i neparnih -> odvojeni ADC offseti (edcg/odcg u
//                                st_gain_offset nisu slucajno u parovima)
//   pad svetla ka ivicama    -> shading po redu
//   razmak RGB redova        -> LineOffsetCorrector iz G2710-6
//
// Sve je DETERMINISTICKO: ista pozicija daje isti red, bez obzira na redosled
// poziva. Test koji padne pada svaki put.

#pragma once

#include "TestTarget.h"
#include "VirtualLamp.h"

#include <cstddef>
#include <cstdint>
#include <span>

namespace g2710::sim {

enum class CcdChannel : int {
    Red = 0,
    Green = 1,
    Blue = 2,
};

struct CcdImperfections {
    // Po-pikselno pojacanje varira +-ovoliko oko 1.0.
    double pixelGainSpread = 0.06;

    // Tamna struja: osnovni nivo i rasipanje, u brojacima.
    double darkOffsetBase = 380.0;
    double darkOffsetSpread = 120.0;

    // Parni i neparni pikseli idu kroz razdvojene ADC lance i ne ponasaju se
    // isto. Zato referenca ima odvojene even/odd offsete.
    double evenOddGainDelta = 0.035;
    double evenOddOffsetDelta = 45.0;

    // Pad osvetljenja ka ivicama reda, kao udeo centra.
    double lampFalloff = 0.12;

    // Razmak izmedju R, G i B redova senzora, u jedinicama 2400 dpi.
    // Profil za G2710 kaze 64 (cfg_sensor_get line_distance).
    int lineDistanceAt2400 = 64;
};

struct CcdGeometry {
    // Optical rezolucija senzora. G2710: 2400 dpi.
    int sensorResolution = 2400;

    // Rezolucija motora, koja odredjuje sta je jedan korak po Y osi.
    int motorResolution = 1200;

    // Sirina aktivnog polja u milimetrima.
    double widthMm = target::kWidthMm;
};

class VirtualCcd {
public:
    VirtualCcd() = default;
    VirtualCcd(CcdGeometry geometry, CcdImperfections imperfections)
        : geometry_(geometry), imperfections_(imperfections) {}

    const CcdGeometry& geometry() const noexcept { return geometry_; }
    const CcdImperfections& imperfections() const noexcept { return imperfections_; }
    void setImperfections(const CcdImperfections& value) noexcept { imperfections_ = value; }

    // Savrsen senzor - za testove koji zele da izoluju nesto drugo.
    void makeIdeal() noexcept;

    // Broj piksela u redu na datoj rezoluciji.
    std::size_t pixelsPerLine(int resolution) const noexcept;

    // Procitaj jedan sirovi red.
    //
    // `motorPosition` je pozicija glave u koracima motora. `channel` bira red
    // senzora; R, G i B NISU na istom mestu, pa isti motorPosition daje tri
    // razlicita reda mete - upravo greska koju LineOffsetCorrector ispravlja.
    void readLine(int motorPosition, int resolution, CcdChannel channel,
                  const VirtualLamp& lamp, std::span<std::uint16_t> out) const;

    // Pomak reda datog kanala u odnosu na poziciju glave, u koracima motora.
    int channelRowOffset(CcdChannel channel, int resolution) const noexcept;

private:
    double pixelGain(std::size_t pixel) const noexcept;
    double pixelDarkOffset(std::size_t pixel) const noexcept;

    CcdGeometry geometry_{};
    CcdImperfections imperfections_{};
};

}  // namespace g2710::sim
