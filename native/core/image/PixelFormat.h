// Format piksela i pretvaranja nad jednim redom.
//
// Sve ovde su ciste funkcije nad baferima - ni transporta ni uredjaja. Zato se
// ceo modul testira bez simulatora, sto ga cini najjeftinijim delom pipeline-a
// za proveru.

#pragma once

#include "../util/Result.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace g2710::image {

// Vrednosti odgovaraju CM_COLOR / CM_GRAY / CM_LINEART iz hp3900_types.c.
enum class ColorMode : int {
    Color = 0,
    Gray = 1,
    Lineart = 2,
};

const char* toString(ColorMode mode) noexcept;

// Broj kanala po tacki.
constexpr int channelsPerDot(ColorMode mode) noexcept {
    return mode == ColorMode::Color ? 3 : 1;
}

// rts8822.c:8762 RTS_Setup_Depth.
//
// Vrednost koja ide u polje 0x30 registra 0x1CF; sirina kanala se iz nje cita.
enum class DepthCode : int {
    Bits8 = 0,
    Bits12 = 1,
    Bits16 = 2,
    Lineart = 3,
};

struct LineGeometry {
    std::size_t bytesPerLine = 0;
    DepthCode depthCode = DepthCode::Bits8;
};

// Izracunaj duzinu reda i kod dubine, tacno kao RTS_Setup_Depth.
//
// Lineart NE koristi `depth` - jedan bit po tacki, zaokruzeno na bajt.
LineGeometry computeLineGeometry(ColorMode mode, int depth, std::size_t widthInDots) noexcept;

// Osmobitni uzorak u punu 16-bitnu skalu.
//
// Ponavljanje bajta, ne pomeranje: 0xFF mora dati 0xFFFF, a ne 0xFF00.
// Sa pomeranjem najsvetliji piksel nikada ne dostigne punu skalu, pa
// shading racuna pojacanje vece od jedan i tiho posvetli celu sliku.
constexpr std::uint16_t widenToFullScale(std::uint8_t value) noexcept {
    return static_cast<std::uint16_t>((value << 8) | value);
}

// --- parni i neparni pikseli -------------------------------------------------
//
// CCD izbacuje dva isprepletana toka. Razdvajanje i spajanje su inverzne
// operacije i testiraju se kao takve.

Status deinterleaveEvenOdd(std::span<const std::uint16_t> interleaved,
                           std::span<std::uint16_t> evenOut,
                           std::span<std::uint16_t> oddOut);

Status interleaveEvenOdd(std::span<const std::uint16_t> even,
                         std::span<const std::uint16_t> odd,
                         std::span<std::uint16_t> out);

// --- gamma -------------------------------------------------------------------

// Tabela ima 256 ulaza kao u referenci (Gamma_AllocTable).
inline constexpr std::size_t kGammaTableSize = 256;

using GammaTable = std::vector<std::uint8_t>;

// Napravi tabelu za dati eksponent. `gamma` od 1.0 daje identitet.
GammaTable makeGammaTable(double gamma);

// Primeni tabelu na red od 16 bita. Ulaz se skalira na indeks tabele, a izlaz
// nazad na punu skalu - tako gamma radi i kada je izlaz 16-bitni.
Status applyGamma(const GammaTable& table, std::span<std::uint16_t> line);

// --- pretvaranja -------------------------------------------------------------

// Tezine kanala pri pretvaranju u sivo. Referenca za gray koristi SAMO crveni
// kanal (cfg_sensor_get channel_gray je {CL_RED, 0}), pa je to podrazumevano
// ponasanje; luminancija je ponudjena kao izbor za nasu aplikaciju.
enum class GrayMethod {
    RedChannel,   // kao referenca
    Luminance,    // 0.299 R + 0.587 G + 0.114 B
};

// `rgb` je kChannels * pixels, planarno (svi R, pa svi G, pa svi B).
Status toGrayscale(std::span<const std::uint16_t> rgb, std::size_t pixels,
                   GrayMethod method, std::span<std::uint16_t> out);

// Dvopragovni prag iz srt_hp3800_platform_get: BINARYTHRESHOLDH = 100,
// BINARYTHRESHOLDL = 99.
//
// Dva praga daju histerezu: piksel iznad `high` je beo, ispod `low` crn, a
// izmedju zadrzava odluku prethodnog. Jedan prag bi na granici davao sum.
struct LineartThreshold {
    int high = 100;
    int low = 99;
};

// Pakuje jedan bit po pikselu, najvisi bit prvi - kao sto cip isporucuje.
// `out` mora biti (pixels + 7) / 8 bajtova.
Status toLineart(std::span<const std::uint16_t> gray, const LineartThreshold& threshold,
                 int sourceBitDepth, std::span<std::uint8_t> out);

// --- dubina ------------------------------------------------------------------

// Skaliraj sa 16 bita na 8, uz zaokruzivanje umesto odsecanja.
void reduceTo8Bit(std::span<const std::uint16_t> in, std::span<std::uint8_t> out);

}  // namespace g2710::image
