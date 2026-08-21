// Deo simulatora koji isporucuje sliku.
//
// Kljucno pravilo: engine geometriju i format CITA IZ REGISTARA, kroz iste
// adrese kroz koje bi ih citao i cip. Nema testnog kanala kojim bi mu se
// "reklo" sta da skenira. Zbog toga:
//
//   - Ako nas kod zaboravi da upise sirinu, simulator isporuci pogresan red -
//     kao sto bi i hardver.
//   - Ako nas kod upise pomak reda koji ne staje u sest bita, polje se odseca
//     na nulu i simulator vraca NEPORAVNATE kanale. To je D3, reprodukovan a
//     ne zaobidjen.
//
// Drugo pravilo: nista se ne isporucuje dok bit izvrsavanja (0xE800, 0x80)
// nije podignut. Citanje pre pokretanja daje nula bajtova, ne stare podatke.

#pragma once

#include "VirtualCcd.h"
#include "VirtualLamp.h"
#include "VirtualMotor.h"
#include "image/LineOffset.h"
#include "image/PixelFormat.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace g2710::sim {

// Ono sto je engine procitao iz registara u trenutku pokretanja.
struct ScanSetup {
    int left = 0;
    int top = 0;
    int width = 0;
    int height = 0;
    int channelsPerDot = 0;
    image::DepthCode depthCode = image::DepthCode::Bits8;
    bool wideChannel = false;

    // Pomaci procitani iz sestobitnih polja - dakle vec odseceni ako nisu
    // stali.
    image::LineOffsetRegisters lineOffsets;

    std::size_t bytesPerLine = 0;
    bool valid() const noexcept { return width > 0 && height > 0 && channelsPerDot > 0; }
};

class ScanEngine {
public:
    // Procitaj konfiguraciju iz bank-a i pocni. `bank` je sirovi registarski
    // prostor pocev od 0xE800.
    //
    // Vraca false ako konfiguracija nema smisla; simulator tada ostane
    // zaustavljen, sto je ono sto bi i cip uradio - ne bi isporucio nista.
    bool start(std::span<const std::uint8_t> bank, int motorPosition);

    void stop() noexcept;
    void reset() noexcept;

    bool running() const noexcept { return running_; }
    const ScanSetup& setup() const noexcept { return setup_; }

    int linesDelivered() const noexcept { return linesDelivered_; }
    int linesRemaining() const noexcept;

    // Napuni `out` sledecim bajtovima slike. Vraca koliko je stvarno dato;
    // nula znaci da vise nema sta (zaustavljen ili gotov).
    //
    // Motor se pomera JEDNOM po isporucenom redu, kao pri stvarnom skeniranju.
    std::size_t read(std::span<std::byte> out, const VirtualCcd& ccd, const VirtualLamp& lamp,
                     VirtualMotor& motor);

    // Koliko koraka motora nosi jedan red na rezoluciji na kojoj se skenira.
    // Izvedeno iz odnosa rezolucije motora i rezolucije skeniranja.
    int stepsPerLine() const noexcept { return stepsPerLine_; }
    void setStepsPerLine(int steps) noexcept;

private:
    // Napravi jedan sirovi red na tekucoj poziciji.
    void renderLine(const VirtualCcd& ccd, const VirtualLamp& lamp, int motorPosition);

    ScanSetup setup_{};
    bool running_ = false;
    int linesDelivered_ = 0;
    int stepsPerLine_ = 1;

    // Red u bajtovima, i koliko je iz njega vec predato.
    std::vector<std::uint8_t> line_;
    std::size_t linePosition_ = 0;

    // Radni bafer po kanalu, da se ne alocira po redu.
    std::vector<std::uint16_t> channel_;
};

}  // namespace g2710::sim
