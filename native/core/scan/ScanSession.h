// Jedan prolaz skeniranja, od registara do gotovih redova.
//
// Ovo je mesto gde se sve dosadasnje spaja. Plan kaze STA se skenira,
// ScanRegisters to upisuje u cip, a sesija vodi podatke kroz ceo lanac:
//
//   bulk bajtovi
//     -> razdvajanje kanala i dubine        (PixelFormat)
//     -> poravnanje R/G/B redova            (LineOffsetCorrector, kad treba)
//     -> shading po pikselu                 (ShadingCoefficients, ako postoji)
//     -> gamma                              (GammaTable, ako postoji)
//     -> sivo ili lineart                   (PixelFormat)
//     -> suzavanje na izlaznu dubinu
//     -> vodoravno smanjivanje              (Resize)
//     -> uspravno smanjivanje               (VerticalResampler)
//     -> gotov red
//
// Redosled nije proizvoljan. Smanjivanje ide POSLEDNJE i radi nad vec
// pretvorenim redom, kao u referenci (Read_ResizeBlock nad rz->mode, koji je
// izlazni format). Menjanje redosleda promenilo bi zaokruzivanje.
//
// STA OVDE NIJE: pomeranje glave. Sesija pretpostavlja da je glava vec tamo
// gde treba. Pozicioniranje ide kroz MotionGuard i porton Head_Relocate, i to
// je zaseban posao koji H4 kvalifikuje - izmisljati ga ovde znacilo bi
// pomerati tudji motor po pretpostavci.

#pragma once

#include "../calib/ShadingCalibration.h"
#include "../image/PixelFormat.h"
#include "../image/Resize.h"
#include "../rts8822/Rts8822.h"
#include "../rts8822/RegisterFile.h"
#include "../rts8822/ScanRegisters.h"
#include "../transport/ITransport.h"
#include "../util/Cancellation.h"
#include "ScanPlanner.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace g2710::scan {

struct ScanOptions {
    // Prazni koeficijenti znace da se shading NE primenjuje. Sesija to
    // prijavljuje kroz shadingApplied(), da se ne desi da izostanak
    // kalibracije prodje neprimeceno.
    calib::ShadingCoefficients shading;

    // Prazna tabela znaci bez gamma korekcije.
    image::GammaTable gamma;

    // Referenca za sivo koristi SAMO crveni kanal; luminancija je nas izbor.
    image::GrayMethod grayMethod = image::GrayMethod::RedChannel;
};

struct ScanStatistics {
    int hardwareLinesRead = 0;
    int outputLinesProduced = 0;

    // Koliko je redova pojelo softversko poravnanje pre prvog izlaznog reda.
    int alignmentLinesConsumed = 0;

    // Koliko je redova progutalo uspravno smanjivanje.
    int resampleLinesConsumed = 0;

    std::size_t bytesRead = 0;

    // Koliko puta zatvaranje prolaza nije uspelo. Vece od nule znaci da je
    // uredjaj mozda ostao da skenira - podatak koji mora u izvestaj.
    int unclosedPasses = 0;
};

class ScanSession {
public:
    // Sesija sama pravi ScanRegisters i Rts8822 nad istim RegisterFile-om -
    // pozivalac ne mora da slaze tri objekta da bi skenirao jednom.
    ScanSession(rts8822::RegisterFile& registers, SafetyGate gate, const ScanPlan& plan,
                ScanOptions options = {});
    ~ScanSession();

    ScanSession(const ScanSession&) = delete;
    ScanSession& operator=(const ScanSession&) = delete;

    const ScanPlan& plan() const noexcept { return plan_; }

    // Duzina jednog gotovog reda u bajtovima, i koliko ih se ocekuje.
    std::size_t outputBytesPerLine() const noexcept;
    int expectedOutputLines() const noexcept { return expectedLines_; }

    // Da li se shading zaista primenjuje. False znaci da kalibracija nije
    // pokrenuta - slika ce nositi neujednacenost senzora.
    bool shadingApplied() const noexcept { return shadingApplied_; }
    bool gammaApplied() const noexcept { return !options_.gamma.empty(); }

    // Upisi konfiguraciju u cip i pokreni prolaz.
    //
    // Odbija ako je lampa ugasena. Bez toga bi ceo prolaz prosao "uspesno"
    // i dao crnu sliku - otkaz koji se na tudjem racunaru ne bi razlikovao
    // od pokvarenog senzora.
    Status begin();
    bool started() const noexcept { return started_; }

    // Sledeci gotov red. `out` mora biti outputBytesPerLine() bajtova.
    //
    // Vraca true kada je red popunjen, false kada je slika gotova. Greska
    // znaci prekid - otkazivanje, istek roka ili gubitak veze.
    Result<bool> nextLine(std::span<std::uint8_t> out, const CancellationToken& token);

    // Uredno zavrsi prolaz: spusti bit izvrsavanja da cip prestane da
    // skenira.
    //
    // Zove se sam kada nextLine() izda poslednji red. Postoji i javno jer
    // pozivalac sme da odustane ranije.
    //
    // OVO NIJE SITNICA. Bez zatvaranja prolaza cip ostaje da skenira i
    // posle poslednjeg reda koji je iko procitao - glava nastavlja da se
    // krece, a sledeci prolaz zatice zauzet uredjaj i vrati nula redova.
    Status finish();

    // Prekini prolaz nasilno; isto dejstvo, drugo ime za dijagnostiku.
    Status abort();

    // Da li je prolaz zatvoren.
    bool finished() const noexcept { return finished_; }

    const ScanStatistics& statistics() const noexcept { return stats_; }

private:
    // Napuni jedan sirov hardverski red. False znaci da cip vise nema podataka.
    Result<bool> readHardwareLine(const CancellationToken& token);

    // Sirovi red -> planovi po kanalu, u punoj 16-bitnoj skali.
    void decodeChannels();

    // Planovi -> jedan red u izlaznom formatu, na NATIVE sirini.
    void convertToOutputFormat();

    rts8822::RegisterFile& registers_;
    SafetyGate gate_;
    rts8822::ScanRegisters scanRegisters_;
    rts8822::Rts8822 chip_;
    ScanPlan plan_;
    ScanOptions options_;

    bool started_ = false;
    bool finished_ = false;
    bool shadingApplied_ = false;
    int expectedLines_ = 0;

    std::size_t hardwareLineBytes_ = 0;
    std::size_t nativeWidth_ = 0;
    std::size_t outputWidth_ = 0;
    int channelsIn_ = 3;
    int channelsOut_ = 3;
    bool wideChannel_ = false;
    bool lineart_ = false;

    // Bulk stize u komadima; red se sklapa ovde.
    std::vector<std::uint8_t> rawLine_;
    std::size_t rawFilled_ = 0;
    std::vector<std::byte> chunk_;
    bool transportDrained_ = false;

    // Po kanalu, na native sirini.
    std::vector<std::vector<std::uint16_t>> planes_;

    // Poravnanje kanala kada hardver ne moze.
    std::unique_ptr<image::LineOffsetCorrector> corrector_;
    std::vector<std::uint16_t> alignedLine_;

    // Red u izlaznom formatu na native sirini, pa na trazenoj.
    std::vector<std::uint16_t> nativeValues_;
    std::vector<std::uint16_t> resizedValues_;
    std::vector<std::uint8_t> nativeBits_;
    std::vector<std::uint8_t> resizedBits_;

    std::unique_ptr<image::VerticalResampler> resampler_;
    std::unique_ptr<image::VerticalLineartResampler> lineartResampler_;

    ScanStatistics stats_{};
};

}  // namespace g2710::scan
