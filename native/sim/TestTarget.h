// Sinteticka test-meta koju virtuelni CCD "vidi".
//
// Meri se u milimetrima, u istom koordinatnom sistemu kao stvarna povrsina
// skenera (220 x 300 mm za flatbed, cfg_constrains_get). Zahvaljujuci tome
// geometrijske greske - pogresan offset, pogresna rezolucija, pomerena
// referentna pozicija - pomeraju sliku na nacin koji test moze da izmeri.
//
// Raspored je izabran tako da svaka faza koja dolazi ima sta da proveri:
//
//    0 -  5 mm   bela kalibraciona traka   -> white shading
//    5 - 10 mm   crna kalibraciona traka   -> black shading, dark current
//   10 - 40 mm   RGB trake                 -> redosled i razdvajanje kanala
//   40 - 70 mm   horizontalni gradijent    -> gamma, linearnost
//   70 - 100 mm  geometrijska mreza        -> rezolucija i offset
//  100 - 300 mm  neutralna siva            -> ujednacenost polja

#pragma once

#include <cstddef>

namespace g2710::sim {

struct Reflectance {
    double red = 0.0;
    double green = 0.0;
    double blue = 0.0;

    double channel(std::size_t index) const noexcept {
        switch (index) {
            case 0:  return red;
            case 1:  return green;
            default: return blue;
        }
    }
};

// Granice pojaseva, u milimetrima od vrha.
namespace target {
inline constexpr double kWhiteStripTop = 0.0;
inline constexpr double kWhiteStripBottom = 5.0;
inline constexpr double kBlackStripBottom = 10.0;
inline constexpr double kColorBarsBottom = 40.0;
inline constexpr double kGradientBottom = 70.0;
inline constexpr double kGridBottom = 100.0;

inline constexpr double kWidthMm = 220.0;
inline constexpr double kHeightMm = 300.0;

// Referentne vrednosti traka. Bela nije 1.0 jer ni pravi papir nije.
inline constexpr double kWhiteReflectance = 0.95;
inline constexpr double kBlackReflectance = 0.03;
inline constexpr double kNeutralReflectance = 0.50;

inline constexpr double kGridPitchMm = 10.0;
inline constexpr double kGridLineMm = 0.5;
inline constexpr int kColorBarCount = 8;
}  // namespace target

// Refleksija na datoj tacki. Van povrsine vraca crno.
Reflectance sampleTestTarget(double xMillimetres, double yMillimetres) noexcept;

}  // namespace g2710::sim
