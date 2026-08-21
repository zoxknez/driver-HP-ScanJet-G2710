// Smanjivanje slike sa native na trazenu rezoluciju.
//
// Skener ume pet rezolucija: 150, 300, 600, 1200, 2400. Sve ostalo sto se
// nudi - 50, 75, 100, 200 - dobija se skeniranjem na prvoj visoj native
// rezoluciji i smanjivanjem. Vidi ScanPlanner.
//
// Racun je porton iz reference, ne izmisljen:
//
//   vodoravno   Resize_Decrease, rts8822.c:5719
//   uspravno    Read_ResizeBlock, RSZ_DECREASE grana, rts8822.c:6790
//
// Oba su celobrojni resampleri sa akumulatorom, ne bilinearna interpolacija.
// Vodoravni usrednjava povrsinu (svaki ulazni piksel ucestvuje tacno jednom,
// podeljen izmedju najvise dva izlazna); uspravni linearno mesa dva susedna
// reda. Zadrzano je celobrojno deljenje sa istim redosledom operacija, jer bi
// prelazak na pokretni zarez promenio zaokruzivanje i golden testovi ne bi
// vise merili istu stvar.
//
// Suprotan smer (Resize_Increase) NIJE portovan: za G2710 je 2400 dpi i
// najveca native i najveca ponudjena rezolucija, pa je ta grana nedostizna.
// Njena lineart varijanta ima i vidljivu gresku - vidi D4 u
// docs/REFERENCE-DEFECTS.md.

#pragma once

#include "../util/Result.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace g2710::image {

// Sirina izlaza za dati ulaz, po istom celobrojnom racunu koji planer koristi
// za koordinate.
std::size_t resizedWidth(std::size_t fromWidth, int fromResolution,
                         int toResolution) noexcept;

// --- vodoravno ---------------------------------------------------------------

// Smanji jedan red jednog kanala.
//
// `to.size()` odredjuje koliko izlaznih piksela se trazi; funkcija ne pretpo-
// stavlja da je to tacno resizedWidth() - planer sme traziti manje.
Status resizeLineDown(std::span<const std::uint16_t> from, int fromResolution,
                      std::span<std::uint16_t> to, int toResolution);

// Lineart varijanta radi nad pakovanim bitima, najvisi bit prvi.
//
// `from` mora imati (fromWidth + 7) / 8 bajtova, `to` (toWidth + 7) / 8.
Status resizeLineartDown(std::span<const std::uint8_t> from, std::size_t fromWidth,
                         int fromResolution, std::span<std::uint8_t> to,
                         std::size_t toWidth, int toResolution);

// --- uspravno ----------------------------------------------------------------

// Meso dva susedna reda dok se ne dobije red na trazenoj rezoluciji.
//
// Redovi ulaze jedan po jedan; izlaz se ne pojavljuje za svaki ulaz. Za odnos
// 150 -> 100 dpi izlaze dva reda na svaka tri ulazna.
//
// Prvi ulazni red nikada ne proizvodi izlaz - dok ne postoji prethodni red,
// nema se sta mesati. Referenca se oslanja na isto ponasanje (akumulator
// krece od nule, pa prvi prolaz ne moze preci prag).
class VerticalResampler {
public:
    VerticalResampler(int fromResolution, int toResolution, std::size_t valuesPerLine);

    int fromResolution() const noexcept { return from_; }
    int toResolution() const noexcept { return to_; }
    std::size_t valuesPerLine() const noexcept { return values_; }

    bool valid() const noexcept { return values_ > 0 && from_ > 0 && to_ > 0 && to_ <= from_; }

    // Ubaci jedan red na native rezoluciji.
    Status push(std::span<const std::uint16_t> line);

    bool hasOutput() const noexcept { return ready_; }

    // Uzmi red na trazenoj rezoluciji. Prazni pripremljeni izlaz.
    Status pop(std::span<std::uint16_t> out);

    // Koliko je ulaznih redova primljeno i koliko izlaznih izdato.
    int consumedLines() const noexcept { return consumed_; }
    int producedLines() const noexcept { return produced_; }

    // Koliko izlaznih redova se ocekuje za dati broj ulaznih.
    static std::size_t expectedOutputLines(std::size_t inputLines, int fromResolution,
                                           int toResolution) noexcept;

    void reset();

private:
    int from_;
    int to_;
    std::size_t values_;

    std::vector<std::uint16_t> previous_;
    std::vector<std::uint16_t> current_;
    std::vector<std::uint16_t> output_;

    int accumulator_ = 0;
    bool hasPrevious_ = false;
    bool ready_ = false;
    int consumed_ = 0;
    int produced_ = 0;
};

// Uspravno smanjivanje za lineart.
//
// Odvojena klasa jer racun NIJE isti: bitovi se ne mogu mesati, pa referenca
// (rts8822.c:6850) meri koliko je "crnog" upalo iz oba reda i poredi sa
// pragom. Prag je izlazna rezolucija, a ne njena polovina - isto lenient
// pravilo kao vodoravno, iz istog razloga: tanka linija ne sme da nestane.
class VerticalLineartResampler {
public:
    VerticalLineartResampler(int fromResolution, int toResolution, std::size_t widthInPixels);

    int fromResolution() const noexcept { return from_; }
    int toResolution() const noexcept { return to_; }
    std::size_t widthInPixels() const noexcept { return width_; }
    std::size_t bytesPerLine() const noexcept { return (width_ + 7) / 8; }

    bool valid() const noexcept { return width_ > 0 && from_ > 0 && to_ > 0 && to_ <= from_; }

    // Ubaci jedan pakovan red na native rezoluciji.
    Status push(std::span<const std::uint8_t> line);

    bool hasOutput() const noexcept { return ready_; }
    Status pop(std::span<std::uint8_t> out);

    int consumedLines() const noexcept { return consumed_; }
    int producedLines() const noexcept { return produced_; }

    void reset();

private:
    int from_;
    int to_;
    std::size_t width_;

    std::vector<std::uint8_t> previous_;
    std::vector<std::uint8_t> current_;
    std::vector<std::uint8_t> output_;

    int accumulator_ = 0;
    bool hasPrevious_ = false;
    bool ready_ = false;
    int consumed_ = 0;
    int produced_ = 0;
};

}  // namespace g2710::image
