// Pretvaranje zahteva za skeniranje u konkretan plan.
//
// Ulaz je ono sto korisnik trazi - rezolucija, rezim, oblast. Izlaz je red iz
// hp3800_scanmodes plus sve sto iz njega sledi: timing profil, motorna kriva,
// geometrija reda na hardveru i na izlazu, i potreban lookahead za poravnanje
// kanala.
//
// Cist racun. Ne dodiruje uredjaj i ne trazi SafetyGate - planiranje nije
// izvrsavanje.
//
// Dva mesta gde tabela ne daje ono sto se trazi, i gde planer radi isto sto i
// referenca:
//
//   1. Nema lineart redova.  ->  koristi se sivi red (RTS_GetScanmode).
//   2. Nema 50/75/100/200.   ->  skenira se na najblizoj VISOJ native
//                                rezoluciji i slika se smanjuje
//                                (Scanmode_fitres + Resize_Decrease).

#pragma once

#include "../image/LineOffset.h"
#include "../image/PixelFormat.h"
#include "../util/Result.h"
#include "Capabilities.h"

#include "G2710Profile.generated.h"

#include <cstddef>

namespace g2710::scan {

// ST_NORMAL / ST_TA / ST_NEG iz hp3900_types.c.
enum class ScanSource : int {
    Flatbed = 1,
    Tma = 2,
    Negative = 3,
};

const char* toString(ScanSource source) noexcept;

// USB11 = 0, USB20 = 1.
enum class UsbSpeed : int {
    Usb11 = 0,
    Usb20 = 1,
};

// RSZ_NONE / RSZ_DECREASE / RSZ_INCREASE.
enum class ResizeType {
    None,
    Decrease,
    Increase,
};

const char* toString(ResizeType type) noexcept;

// Oblast skeniranja, u pikselima na rezoluciji na koju se odnosi.
struct ScanRegion {
    int left = 0;
    int top = 0;
    int width = 0;
    int height = 0;

    bool empty() const noexcept { return width <= 0 || height <= 0; }
};

struct ScanRequest {
    int resolution = 300;
    image::ColorMode colorMode = image::ColorMode::Color;
    int depth = 8;
    ScanSource source = ScanSource::Flatbed;
    UsbSpeed usb = UsbSpeed::Usb20;

    // Prazna oblast znaci cela povrsina.
    ScanRegion region;

    // Ako je true, plan sme koristiti rezolucije koje nisu hardverski
    // potvrdjene. Dijagnostika i qualification wizard ga postavljaju; WIA i
    // TWAIN nikada.
    bool allowUnqualified = false;
};

struct ScanPlan {
    int requestedResolution = 0;

    // Rezolucija na kojoj se STVARNO skenira. Razlicita od trazene kada za
    // trazenu nema reda u tabeli.
    int nativeResolution = 0;
    ResizeType resize = ResizeType::None;

    // Indeks reda u profile::kScanModes.
    int scanModeIndex = -1;

    // Rezim po kome je red trazen - za lineart je Gray.
    image::ColorMode tableMode = image::ColorMode::Color;

    // Dubina koja ide u hardver. Za lineart je uvek 8, kao u referenci.
    int hardwareDepth = 8;

    // Iz nadjenog reda.
    int timingIndex = 0;
    int motorCurveIndex = -1;
    int sampleRate = 0;
    int systemClock = 0;
    int ctpc = 0;
    int motorBackStep = 0;

    // Oblast na trazenoj rezoluciji (izlazna) i na native rezoluciji (ono sto
    // se salje hardveru).
    ScanRegion requestedRegion;
    ScanRegion nativeRegion;

    // Duzina reda kakvu isporucuje hardver, i kakvu ocekuje pozivalac.
    image::LineGeometry hardwareLine;
    image::LineGeometry outputLine;

    // Referenca udvostrucava sirinu za sivo kada red radi na PIXEL_RATE
    // (rts8822.c:1725). Za G2710 su svi sivi redovi LINE_RATE, pa je ovo uvek
    // false; drzi se eksplicitno da port ostane veran i proverljiv.
    bool grayPixelRateDoubling = false;

    // Poravnanje razmaknutih R/G/B redova, racunato na native rezoluciji.
    image::LineOffsetRegisters lineOffsets;
    bool useHardwareAlignment = false;
    int softwareLineDistance = 0;
    int alignmentLookahead = 0;

    const profile::ScanMode& scanMode() const;
};

// Napravi plan ili objasni zasto ne moze.
//
// Odbija:
//   - rezoluciju koja nije u tabeli mogucnosti
//   - rezoluciju koja nije hardverski potvrdjena, osim uz allowUnqualified
//   - TMA i negativ (nisu u obimu 1.0)
//   - dubinu koja nije 8 ili 16
//   - oblast van povrsine
Result<ScanPlan> planScan(const ScanRequest& request);

// Podrazumevana oblast: cela flatbed povrsina na datoj rezoluciji.
ScanRegion fullFlatbedRegion(int resolution) noexcept;

}  // namespace g2710::scan
