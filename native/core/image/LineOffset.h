// Razmak R, G i B redova senzora.
//
// Tri boje se ne citaju sa istog mesta - redovi senzora su fizicki razmaknuti
// za `line_distance` (64 na 2400 dpi za G2710). Ako se to ne ispravi, boje su
// pomerene jedna u odnosu na drugu i slika ima obojene rubove.
//
// RTS8822 to ume da radi u hardveru, kroz pet 6-bitnih polja (rts8822.c:8701).
// Sest bita je 63, a G2710 na 1200 dpi trazi 64 - i tu prestaje da radi.
// Vidi D3 u docs/REFERENCE-DEFECTS.md.
//
// Zato ovde postoje OBA puta: hardverski, koji odbija vrednost koja ne staje,
// i softverski, koji nema to ogranicenje.

#pragma once

#include "../util/Result.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace g2710::image {

inline constexpr std::size_t kChannels = 3;

// Sirina polja u registrima 0x149..0x14D.
inline constexpr int kHardwareOffsetBits = 6;
inline constexpr int kHardwareOffsetMax = (1 << kHardwareOffsetBits) - 1;  // 63

// Pet vrednosti koje idu u Regs[0x149..0x14D].
struct LineOffsetRegisters {
    int evenOdd = 0;                 // 0x149
    int lineDistance = 0;            // 0x14A
    int lineDistancePlusEvenOdd = 0; // 0x14B
    int doubleLineDistance = 0;      // 0x14C
    int doublePlusEvenOdd = 0;       // 0x14D

    // Da li sve vrednosti staju u 6 bita.
    bool fitsInHardware() const noexcept;

    // Najveca vrednost - dijagnostika za H8.
    int largest() const noexcept;
};

// Izracunaj vrednosti za datu rezoluciju.
//
// `sensorLineDistance` i `sensorEvenOddDistance` su izrazeni na
// `sensorResolution` (2400 za G2710); skaliraju se na `scanResolution`.
//
// `highResolution` je (scanResolution > 1200), kao u referenci; ispod toga se
// even/odd razmak NE koristi.
LineOffsetRegisters computeLineOffsets(int sensorLineDistance,
                                       int sensorEvenOddDistance,
                                       int sensorResolution, int scanResolution,
                                       bool highResolution) noexcept;

// Da li se na datoj rezoluciji sme koristiti hardversko poravnanje.
//
// Referenca ovo NE proverava - upisuje vrednost kroz masku 0x3F i ona se tiho
// odseca na nulu, cime kanal ostaje nepomeren. Mi to odbijamo.
bool hardwareAlignmentSupported(int sensorLineDistance, int sensorEvenOddDistance,
                                int sensorResolution, int scanResolution,
                                bool highResolution) noexcept;

// --- softversko poravnanje ---------------------------------------------------

// Slaze tri kanala nazad u poravnate redove.
//
// Kanali stizu pomereni: crveni je "napred", plavi "nazad" za dva razmaka.
// Korektor kasni crveni i zeleni dok se ne poklope sa plavim, pa je za prvi
// izlazni red potrebno `requiredLookahead()` ulaznih redova po kanalu.
class LineOffsetCorrector {
public:
    LineOffsetCorrector(std::size_t pixelsPerLine, int lineDistanceInLines);

    std::size_t pixelsPerLine() const noexcept { return pixels_; }
    int lineDistance() const noexcept { return distance_; }

    // Koliko ulaznih redova treba pre nego sto izadje prvi poravnat red.
    int requiredLookahead() const noexcept { return distance_ * 2; }

    // Ubaci jedan red datog kanala.
    Status push(std::size_t channel, std::span<const std::uint16_t> line);

    // Da li je spreman poravnat red.
    bool hasOutput() const noexcept;

    // Uzmi poravnat red. `out` mora biti kChannels * pixelsPerLine.
    Status pop(std::span<std::uint16_t> out);

    // Koliko je poravnatih redova do sada izdato.
    int producedLines() const noexcept { return produced_; }

    void reset();

private:
    std::size_t pixels_;
    int distance_;

    // Red cekanja po kanalu; crveni i zeleni cekaju da ih plavi sustigne.
    std::array<std::vector<std::vector<std::uint16_t>>, kChannels> queues_;
    int produced_ = 0;
};

}  // namespace g2710::image
