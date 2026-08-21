// Upis geometrije i formata skeniranja u registre.
//
// Ovo je most izmedju plana (scan::ScanPlan, cist racun) i cipa. Plan kaze sta
// se skenira; ovaj sloj to pretvara u konkretne bajtove na konkretnim
// adresama, tacno kako to radi referenca:
//
//   koordinate    RTS_Setup_Coords, rts8822.c:9229
//   kanali        RTS_Setup_Channels, rts8822.c:9019 (deo koji pise Regs[0x12])
//   dubina        RTS_Setup_Depth, rts8822.c:8768
//   prag          Scan_Start, rts8822.c:8390
//   pokretanje    RTS_Execute, rts8822.c:3947
//
// Nivo 4 (CCD akvizicija) za sve sto konfigurise senzor, nivo 5 za pokretanje.
// Konfigurisanje bez pokretanja je namerno odvojeno: qualification wizard u H5
// treba da postavi geometriju i procita je nazad, a da se nista ne pomeri.

#pragma once

#include "../device/SafetyLevel.h"
#include "../image/LineOffset.h"
#include "../image/PixelFormat.h"
#include "RegisterFile.h"

#include <chrono>
#include <cstdint>

namespace g2710::rts8822 {

// Ono sto cip mora da zna o jednom prolazu. Sve u pikselima na rezoluciji na
// kojoj se stvarno skenira.
struct ScanGeometry {
    int left = 0;
    int top = 0;
    int width = 0;
    int height = 0;

    int right() const noexcept { return left + width; }
    int bottom() const noexcept { return top + height; }
};

// Koordinate u registrima NISU u pikselima skeniranja.
//
// rts8822.c:9150 mnozi vodoravne odnosom rezolucija (dakle izrazava ih u
// jedinicama SENZORA), a uspravne brojem dummy redova. Uspravna visina se uz
// to jos i PRODUZAVA: cip mora da preskenira onoliko redova koliko poravnanje
// kanala pojede pre nego sto prvi ispravan red izadje.
//
// Bez ove transformacije registri deluju tacno a slika ispada uza i kraca
// nego sto je trazeno - greska koja se vidi tek na hardveru.
struct CoordinateScaling {
    // Regs[0x0C0] & 0x1F - sensorResolution / scanResolution.
    int resolutionRatio = 1;

    // Regs[0x0D6] >> 4.
    int dummyLine = 1;

    // Regs[0x14D] & 0x3F - koliko redova pojede hardversko poravnanje.
    int lineOffsetPadding = 0;

    // Koliko redova pojede softversko poravnanje, kada je ono na redu.
    int softwareLineDistance = 0;

    bool valid() const noexcept { return resolutionRatio > 0 && dummyLine > 0; }
};

// Iz piksela skeniranja u jedinice registara i nazad.
//
// Povratak nije potpuna inverzija: visina u registrima nosi i produzenje, pa
// `fromRegisterCoordinates` vraca broj redova koji cip STVARNO skenira, a ne
// onaj koji je pozivalac trazio. Za simulator je bas to tacan podatak.
ScanGeometry toRegisterCoordinates(const ScanGeometry& pixels,
                                   const CoordinateScaling& scaling) noexcept;
ScanGeometry fromRegisterCoordinates(const ScanGeometry& registers,
                                     const CoordinateScaling& scaling) noexcept;

struct ScanFormat {
    int channelsPerDot = 3;
    image::DepthCode depthCode = image::DepthCode::Bits8;

    // Prag za lineart. Ignorise se u ostalim rezimima, ali se upisuje uvek -
    // referenca ga upisuje bezuslovno u Scan_Start.
    image::LineartThreshold threshold;

    // Samo za citanje: da li registar sirine kanala kaze dva bajta.
    // setFormat() ga izvodi iz depthCode i ne cita ovo polje.
    bool wideChannel = false;
};

// Koliko bajtova cip isporucuje po redu za datu geometriju i format.
std::size_t bytesPerLine(const ScanGeometry& geometry, const ScanFormat& format) noexcept;

class ScanRegisters {
public:
    ScanRegisters(RegisterFile& registers, SafetyGate gate)
        : registers_(registers), gate_(gate) {}

    // --- nivo 4: konfiguracija --------------------------------------------

    // rts8822.c:9229. Uspravne koordinate su 20-bitne: donjih 16 u paru, gornja
    // 4 bita u niblovima jednog zajednickog bajta.
    Status setGeometry(const ScanGeometry& geometry);
    Result<ScanGeometry> geometry();

    // Kanala po tacki i dubina po kanalu.
    Status setFormat(const ScanFormat& format);
    Result<ScanFormat> format();

    // rts8822.c:8701. Pet sestobitnih polja.
    //
    // Upisuje kroz masku 0x3F, tacno kao referenca - dakle vrednost koja ne
    // staje TIHO se odseca. To nije previd nego doslovan prenos: tako nastaje
    // D3, i tako se on moze videti na hardveru umesto da se pretpostavlja.
    // Ko hoce da to izbegne, proverava fitsInHardware() PRE poziva; planer to
    // i radi (ScanPlan::useHardwareAlignment).
    Status setLineOffsets(const image::LineOffsetRegisters& offsets);
    Result<image::LineOffsetRegisters> lineOffsets();

    // Iskljucuje hardversko poravnanje - svih pet polja na nulu, kao u grani
    // FIX_BY_SOFT reference.
    Status clearLineOffsets();

    // Odnos rezolucija i broj dummy redova - dve vrednosti bez kojih se
    // koordinate iz registara ne mogu protumaciti.
    Status setResolutionRatio(int ratio);
    Result<int> resolutionRatio();
    Status setDummyLine(int dummyLine);
    Result<int> dummyLine();

    // --- nivo 5: pokretanje ------------------------------------------------

    // rts8822.c:3947 RTS_Execute. Sest upisa u tacnom redosledu preko dva
    // registra; poslednji podize bit izvrsavanja.
    //
    // Redosled NIJE proizvoljan i ne sme se skratiti: 0xE813 i 0xE800 se
    // naizmenicno menjaju, pa tek na kraju 0xE800 dobija bit 0x80.
    Status execute();

    // rts8822.c:3843 RTS_IsExecuting.
    Result<bool> isExecuting();

    // rts8822.c:3868 RTS_WaitScanEnd - anketira dok se bit izvrsavanja ne
    // spusti, ili dok rok ne istekne.
    Status waitScanEnd(std::chrono::milliseconds timeout);

    // rts8822.c:3913 RTS_Warm_Reset - postavi pa spusti bit 0x40.
    Status warmReset();

private:
    RegisterFile& registers_;
    SafetyGate gate_;
};

}  // namespace g2710::rts8822
