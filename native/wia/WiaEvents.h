// Fizicka dugmad -> STI dogadjaji.
//
// Uredjaj sa interrupt pipe-a vraca MASKU; STI ocekuje GUID. Prevod je ovde,
// odvojen od COM ljuske, jer je to jedini deo koji se moze pogresiti - i jer
// mora ostati saglasan sa Events podkljucem u driver/g2710.inf.
//
// Maske dolaze iz profila (kButtons, izvedeno iz cfg_buttons_get):
//
//   0x01  Scan
//   0x02  Copy
//   0x04  PDF
//
// KOJE dugme nosi koju masku je PRETPOSTAVKA dok H10 ne progovori. Zapisana je
// i ovde i u INF-u, na istom mestu gde bi se ispravila.

#pragma once

#include <windows.h>

#include <cstdint>
#include <span>

namespace g2710::wia {

struct ButtonEvent {
    std::uint32_t mask;
    const GUID* eventGuid;

    // Ime iz INF Events podkljuca. Sluzi za dijagnostiku i za test koji
    // poredi kod sa INF-om.
    const wchar_t* infName;
};

// Sva podrzana dugmad, redom kojim su u profilu.
std::span<const ButtonEvent> buttonEvents() noexcept;

// Maska -> dogadjaj. Nullptr za nepoznatu masku.
//
// Uredjaj sme prijaviti vise dugmadi odjednom (npr. drzana dva); uzima se
// PRVO poznato, jer STI nosi tacno jedan GUID po obavestenju.
const ButtonEvent* eventForMask(std::uint32_t mask) noexcept;

// Koliko dugmadi profil prijavljuje. Ako se razidje sa buttonEvents(), jedno
// od dva je zastarelo.
int buttonCountFromProfile() noexcept;

}  // namespace g2710::wia
