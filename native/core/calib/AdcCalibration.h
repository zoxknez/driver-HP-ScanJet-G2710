// ADC pojacanje i offset.
//
// Analogni lanac senzora ima dva podesavanja pre nego sto shading uopste ima
// smisla:
//
//   VGAG  pojacanje - koliko se signal pojacava pre pretvaranja
//   DCG   offset    - gde je "crno", odvojeno za parne i neparne piksele
//
// Shading iz ShadingCalibration ispravlja ono sto ostane PO pikselu; ovo
// namesta ceo lanac. Redosled je bitan: pogresno pojacanje isece vrhove i
// nijedan po-pikselni koeficijent to vise ne moze da vrati.
//
// Portovano iz Calib_AdcGain (rts8822.c:11587) i Calib_AdcOffsetRT (:12153).
//
// Kao i ShadingCalibration, ovo je CIST RACUN nad izmerenim vrednostima -
// merenje obavlja pozivalac. Zato se ceo modul testira bez uredjaja.
//
// Dva nalaza iz reference su zapisana u docs/REFERENCE-DEFECTS.md kao D5 i D6;
// oba se ticu celobrojnog deljenja tamo gde racun trazi realno.

#pragma once

#include "../util/Result.h"
#include "CalibrationConfig.h"

#include "G2710Profile.generated.h"

#include <array>
#include <cstddef>
#include <span>

namespace g2710::calib {

// --- prozor u kome se meri offset ---------------------------------------------

struct OffsetWindow {
    int left = 0;
    int pixels = 0;

    bool valid() const noexcept { return pixels > 0; }
};

// Iz kOffsets u profilu (hp3800_offset, hp3900_config.c:966).
//
// Prozor postoji samo za native rezolucije; za ostale nema reda, pa se vraca
// prazan i pozivalac mora da odluci - a ne da dobije tudje vrednosti.
OffsetWindow offsetWindow(int resolution, CalibrationSection section) noexcept;

// --- pojacanje i offset kao stanje --------------------------------------------

// Sest DCG vrednosti (tri kanala x parno/neparno) i tri VGAG.
//
// DCG je devetobitni registarski oblik sa prelomom na 0x100 - vidi
// decodeDcg/encodeDcg.
struct GainOffsetState {
    std::array<int, kChannels> evenDcg{};
    std::array<int, kChannels> oddDcg{};
    std::array<int, kChannels> gain{};  // VGAG, 0..31

    // Da li je kanal iscrpeo podesavanje i vise nema sta da menja.
    std::array<bool, kChannels> evenReady{};
    std::array<bool, kChannels> oddReady{};

    bool allReady() const noexcept;
};

// Pocetne vrednosti iz profila (kGainOffsets). Za G2710 su sva pojacanja 4.
GainOffsetState initialGainOffset(int usbSpeed = 1) noexcept;

// Registarski oblik <-> predznacena vrednost.
//
// Referenca koristi dve razlicite konstante za dva smera (0xFF unapred, 0x100
// unazad), pa preslikavanje NIJE inverzno. Preneto doslovno; vidi D6.
int decodeDcg(int registerValue) noexcept;
int encodeDcg(int signedValue) noexcept;

// --- ADC pojacanje ------------------------------------------------------------

// Koje deljenje se koristi u formuli za pojacanje.
//
// Referenca deli CELOBROJNO tamo gde ceo ostatak racuna radi u pokretnom
// zarezu (D5). Pri podrazumevanom pojacanju 4 razlika ne postoji, ali cim se
// pojacanje pomeri - postoji, i preko 4 faktor pada na nulu.
enum class GainArithmetic {
    Reference,  // kao hp3900: (44 - gain) / 40 celobrojno
    Corrected,  // (44.0 - gain) / 40.0
};

const char* toString(GainArithmetic arithmetic) noexcept;

struct AdcGainResult {
    std::array<int, kChannels> gain{};

    // Faktor kojim je racun mnozio - izlozen jer je bas on sporan u D5.
    std::array<double, kChannels> appliedFactor{};

    // Da li je bar jedan kanal dosegao prag iz reference (vrh >= cilj + 5).
    // Referenca po tome odlucuje da li je kalibracija uspela.
    bool reachedTarget = false;
};

// Izracunaj novo pojacanje iz izmerenih vrhova.
//
// `peakAverage` je po kanalu prosek maksimuma po koloni, u skali 0..255.
// `depth` je dubina u kojoj je mereno (referenca uvek meri na 8 bita).
Result<AdcGainResult> computeAdcGain(const CalibrationConfig& config,
                                     std::span<const double> peakAverage,
                                     const GainOffsetState& state,
                                     int depth = 8,
                                     GainArithmetic arithmetic = GainArithmetic::Reference);

// --- ADC offset ---------------------------------------------------------------

// Koje polje se podesava u ovom prolazu.
enum class OffsetParity {
    Even,
    Odd,
};

struct AdcOffsetStep {
    // Da li je bilo izmene; dok je true, petlja se ponavlja (do_loop).
    bool changed = false;

    // Koliko je kanala proglaseno gotovim u ovom koraku.
    int settled = 0;

    // Koliko je puta pojacanje moralo da se smanji jer offset nije stao.
    int gainReductions = 0;
};

// Jedan korak povratne petlje, za jednu parnost.
//
// `channelSum` je zbir izmerenih uzoraka tog kanala u prozoru, u skali 0..255.
// Duzina mora biti broj kanala.
//
// OVO JE GRANA ZA VISOKU REZOLUCIJU (rts8822.c:12290). Za G2710 je ona jedina
// dostizna: referenca bira granu po `sensorresolution >= 1200`, a senzor je
// 2400 - dakle uvek. Niskorezolucijska grana je mrtva na ovom uredjaju i nije
// portovana; njen racun ima i deljenje nulom (D6).
Result<AdcOffsetStep> advanceAdcOffset(const CalibrationConfig& config,
                                       OffsetParity parity,
                                       std::span<const double> channelSum,
                                       int samplesUsed, GainOffsetState* state);

// Ciljna vrednost za offset, u istoj skali u kojoj radi advanceAdcOffset.
// Referenca je pomera za osam bita, a nulu zamenjuje sa 0x80.
int offsetTargetFor(const CalibrationConfig& config, std::size_t channel) noexcept;

}  // namespace g2710::calib
