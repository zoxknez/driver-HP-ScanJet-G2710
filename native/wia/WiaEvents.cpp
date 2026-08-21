#include "WiaEvents.h"

#include <wia.h>

#include <wiadef.h>

#include "G2710Profile.generated.h"

namespace g2710::wia {
namespace {

// Redosled prati kButtons iz profila: maska 0x01, 0x02, 0x04.
constexpr ButtonEvent kEvents[] = {
    {0x01u, &WIA_EVENT_SCAN_IMAGE, L"Scan"},
    {0x02u, &WIA_EVENT_SCAN_PRINT_IMAGE, L"Copy"},
    {0x04u, &WIA_EVENT_SCAN_OCR_IMAGE, L"Pdf"},
};

}  // namespace

std::span<const ButtonEvent> buttonEvents() noexcept {
    return {kEvents, std::size(kEvents)};
}

const ButtonEvent* eventForMask(std::uint32_t mask) noexcept {
    // Nema posebne provere za masku nula: petlja tada nijednom ne pogodi i
    // ionako vraca nullptr. Ranija verzija je imala takvu proveru, ali je
    // nijedan test nije mogao razlikovati od njenog odsustva - a kod koji se
    // ne moze proveriti ne treba da postoji.
    for (const auto& event : kEvents) {
        if ((mask & event.mask) != 0) {
            return &event;
        }
    }
    return nullptr;
}

int buttonCountFromProfile() noexcept { return profile::kButtons.count; }

}  // namespace g2710::wia
