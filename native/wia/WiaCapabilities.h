// Sta WIA nudi Windowsu, i sta prihvata nazad.
//
// Ovo je sloj ODLUKA, odvojen od COM-a namerno. `IWiaMiniDrv` je ljuska koja
// prevodi pozive; sve sto se moze pogresiti - koje rezolucije se nude, sta se
// desava kada aplikacija trazi nesto van opsega, kako se oblast skeniranja
// pretvara u piksele - stoji ovde i testira se bez ijednog COM objekta.
//
// PRAVILO KOJE OVAJ FAJL SPROVODI:
//
//   WIA oglasava ISKLJUCIVO ono sto je HARDWARE_VALIDATED.
//
// Na dan pisanja to je NISTA - skener jos nije bio prikljucen, pa je
// scan::advertisableResolutions() prazna. Drajver zato mora imati odgovor na
// pitanje "sta ako nema nijedne rezolucije", i taj odgovor ne sme biti prazna
// lista ponudjena Windowsu: aplikacija bi je prikazala kao pokvaren uredjaj.
//
// Odgovor je kvalifikacioni build, po istom obrascu kao BuildSafetyCeiling:
//
//   G2710_WIA_ALLOW_UNQUALIFIED = 0   izdanje - nudi se samo potvrdjeno
//   G2710_WIA_ALLOW_UNQUALIFIED = 1   paket za H11 - nudi se i nepotvrdjeno
//
// Prekidac je KOMPAJLERSKI, ne runtime. Binarni fajl koji ide korisniku ne
// sme imati nacin da se prevede u rezim koji obecava ono sto nije provereno.

#pragma once

#include "scan/Capabilities.h"
#include "scan/ScanPlanner.h"
#include "util/Result.h"

#include <cstdint>
#include <span>
#include <string>
#include <vector>

#ifndef G2710_WIA_ALLOW_UNQUALIFIED
#define G2710_WIA_ALLOW_UNQUALIFIED 0
#endif

namespace g2710::wia {

// Da li ovaj build nudi i ono sto hardver nije potvrdio.
inline constexpr bool kAllowUnqualified = G2710_WIA_ALLOW_UNQUALIFIED != 0;

// WIA_DATA_* vrednosti iz WiaDef.h. Ponovljene ovde da se sloj odluka moze
// testirati bez SDK zaglavlja; WiaTypesMatchTheSdk test drzi ih saglasnim.
enum class WiaDataType : int {
    Threshold = 0,  // WIA_DATA_THRESHOLD
    Grayscale = 2,  // WIA_DATA_GRAYSCALE
    Color = 3,      // WIA_DATA_COLOR
};

const char* toString(WiaDataType type) noexcept;

// Prevod u nas rezim i nazad.
image::ColorMode toColorMode(WiaDataType type) noexcept;
WiaDataType toWiaDataType(image::ColorMode mode) noexcept;

// WIA_IPA_DEPTH je bita po TACKI, ne po kanalu. Boja 8 bita po kanalu je 24.
int wiaDepthFor(image::ColorMode mode, int bitsPerChannel) noexcept;
int bitsPerChannelFrom(image::ColorMode mode, int wiaDepth) noexcept;

// --- sta se nudi ----------------------------------------------------------------

// Rezolucije koje drajver sme da prikaze. Prazna lista je legitiman ishod u
// izdanju pre hardverske kvalifikacije; pozivalac to mora obraditi.
std::vector<int> offeredResolutions();

// Da li drajver uopste moze da radi. False znaci da nema nijedne potvrdjene
// rezolucije, pa se uredjaj ne sme predstaviti kao upotrebljiv.
bool hasUsableConfiguration();

// Podrazumevana rezolucija: 300 ako je ponudjena, inace prva ponudjena.
int defaultResolution();

std::span<const WiaDataType> offeredDataTypes() noexcept;
std::span<const int> offeredBitDepths(WiaDataType type) noexcept;

// Najveca povrsina u hiljaditim delovima inca (WIA_IPS_MAX_HORIZONTAL_SIZE se
// izrazava u pikselima, ali granice povrsine dolaze iz milimetara profila).
struct SurfaceMillimetres {
    int width = 0;
    int height = 0;
};
SurfaceMillimetres flatbedSurface() noexcept;

// --- sta se prihvata -------------------------------------------------------------

// Vrednosti koje aplikacija postavlja na Flatbed stavku.
struct ItemSettings {
    int xResolution = 300;
    int yResolution = 300;
    WiaDataType dataType = WiaDataType::Color;
    int wiaDepth = 24;

    // Oblast u pikselima NA TEKUCOJ rezoluciji, kao sto WIA i definise.
    int xPosition = 0;
    int yPosition = 0;
    int xExtent = 0;
    int yExtent = 0;

    // Nula znaci "cela povrsina" - koristi se pri inicijalizaciji stavke.
    bool extentUnset() const noexcept { return xExtent <= 0 || yExtent <= 0; }
};

// Zasto je vrednost odbijena. Preslikava se u WIA_ERROR_* u COM sloju.
enum class Rejection {
    None,
    ResolutionNotOffered,
    ResolutionsDiffer,   // WIA dozvoljava X != Y; nas hardver ne
    DepthNotOffered,
    RegionOutsideSurface,
    EmptyRegion,
    NoUsableConfiguration,
};

const char* toString(Rejection reason) noexcept;

struct Validation {
    Rejection reason = Rejection::None;

    // Vrednosti posle popravke. WIA od drajvera OCEKUJE da ispravi ono sto
    // moze umesto da odbije - drvValidateItemProperties zato vraca i ispravku.
    ItemSettings corrected;

    bool accepted() const noexcept { return reason == Rejection::None; }
    bool changed = false;
};

// Popuni podrazumevane vrednosti za novu stavku.
ItemSettings defaultSettings();

// Proveri i popravi. Sve sto se moze zaokruziti na dozvoljenu vrednost -
// zaokruzuje se; sto ne moze - odbija.
Validation validate(const ItemSettings& settings);

// Pretvori proverene vrednosti u zahtev za skeniranje.
//
// Ovo je jedino mesto gde WIA sloj dodiruje planer, i zato jedino mesto gde
// `allowUnqualified` sme biti postavljen - i to samo u kvalifikacionom
// build-u.
Result<scan::ScanRequest> toScanRequest(const ItemSettings& settings);

// Koliko bajtova jedna slika zauzima sa datim vrednostima. WIA to trazi
// unapred (WIA_IPA_ITEM_SIZE) i aplikacija po tome alocira.
Result<std::uint64_t> imageSizeInBytes(const ItemSettings& settings);

}  // namespace g2710::wia
