// Sta uredjaj ume, i koliko toga smemo da tvrdimo.
//
// Iz MASTER plana: svaka mogucnost nosi TRI statusa, ne jedan.
//
//   IMPLEMENTED          kod postoji
//   REFERENCE_VALIDATED  ponasanje odgovara hp3900 referenci
//   HARDWARE_VALIDATED   potvrdjeno na fizickom G2710
//
// WIA i TWAIN oglasavaju ISKLJUCIVO ono sto je HARDWARE_VALIDATED. Sve ostalo
// postoji u kodu i moze se pozvati kroz dijagnostiku, ali se korisniku ne nudi.
//
// Razlog je konkretan: 1200 i 2400 dpi imaju pun kod i pune tabele, ali i
// defekt D3 (docs/REFERENCE-DEFECTS.md) koji objasnjava zasto ih je autor
// reference iskljucio. Oglasiti ih pre nego sto H8 progovori znacilo bi
// obecati nesto za sta imamo razlog da verujemo da ne radi.

#pragma once

#include "../image/PixelFormat.h"
#include "../util/Result.h"

#include <cstddef>
#include <span>
#include <vector>

namespace g2710::scan {

enum class ValidationLevel {
    NotImplemented,
    Implemented,
    ReferenceValidated,
    HardwareValidated,
};

const char* toString(ValidationLevel level) noexcept;

// Kako se rezolucija postize.
enum class ResolutionOrigin {
    Native,   // postoji red u hp3800_scanmodes
    Resized,  // softversko smanjivanje iz vise native rezolucije
};

const char* toString(ResolutionOrigin origin) noexcept;

struct ResolutionCapability {
    int dpi = 0;
    ResolutionOrigin origin = ResolutionOrigin::Native;
    ValidationLevel level = ValidationLevel::NotImplemented;

    // Ako je Resized: iz koje native rezolucije se smanjuje.
    int sourceDpi = 0;

    // Kratko obrazlozenje za dijagnostiku i STATUS.md.
    const char* note = "";

    bool advertisable() const noexcept {
        return level == ValidationLevel::HardwareValidated;
    }
};

// Cela tabela za flatbed. Redosled je rastuci po dpi.
std::span<const ResolutionCapability> flatbedResolutions() noexcept;

// Rezolucije koje se smeju izloziti kroz WIA i TWAIN.
std::vector<int> advertisableResolutions();

// Sve rezolucije koje kod moze da izvrsi, ukljucujuci neoglasene. Koristi
// dijagnostika i qualification wizard.
std::vector<int> executableResolutions();

const ResolutionCapability* findResolution(int dpi) noexcept;

// Najveca dubina po kanalu za dati rezim, i njen status.
struct DepthCapability {
    int bits = 8;
    ValidationLevel level = ValidationLevel::NotImplemented;
    const char* note = "";
};

std::span<const DepthCapability> depthCapabilities() noexcept;

// U hp3800_scanmodes NEMA nijednog lineart reda - lineart se izvodi iz sivog.
// Vraca rezim ciji se red zaista trazi u tabeli.
image::ColorMode tableColorMode(image::ColorMode mode) noexcept;

// Najveca i najmanja rezolucija na kojoj dati rezim ima native red u tabeli.
// Za lineart odgovaraju vrednostima za sivo.
int maxNativeResolution(image::ColorMode mode) noexcept;
int minNativeResolution(image::ColorMode mode) noexcept;

}  // namespace g2710::scan
