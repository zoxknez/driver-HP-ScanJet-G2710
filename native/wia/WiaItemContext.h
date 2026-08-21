// Kontekst jedne WIA stavke.
//
// WIA servis za svaku stavku drzi neprozirni blok bajtova koji drajver sam
// oblikuje i dobija nazad pri svakom pozivu. Ovo je taj blok.
//
// Sadrzi dve stvari:
//
//   - koja je stavka (koren ili flatbed)
//   - sta je aplikacija podesila, i sesija dok prenos traje
//
// Zivotni vek je u rukama WIA servisa: pravi se u drvInitializeWia, oslobadja
// u drvFreeDrvItemContext. Zato nema pametnog pokazivaca na sebe - postoji
// `create` i `destroy`, i par se mora poklopiti.

#pragma once

#include "WiaCapabilities.h"

#include "rts8822/RegisterFile.h"
#include "scan/ScanSession.h"
#include "util/Cancellation.h"

#include <cstdint>
#include <memory>

namespace g2710 {
class G2710Device;
}

namespace g2710::wia {

enum class ItemKind {
    Root,
    Flatbed,
};

const char* toString(ItemKind kind) noexcept;

// Potpis na pocetku bloka. WIA servis vraca sirov `BYTE*`; ako se ikada
// pomesaju konteksti dva drajvera ili dodje ostecen pokazivac, ovo je jedina
// odbrana pre nego sto se pristupi poljima.
inline constexpr std::uint32_t kItemContextSignature = 0x47323731u;  // "G271"

struct ItemContext {
    std::uint32_t signature = kItemContextSignature;
    ItemKind kind = ItemKind::Root;

    // Vrednosti koje je aplikacija postavila. Vaze samo za Flatbed.
    ItemSettings settings;

    // Prenos u toku. Postoji samo izmedju pocetka i kraja jednog prenosa.
    std::unique_ptr<rts8822::RegisterFile> registers;
    std::unique_ptr<scan::ScanSession> session;
    CancellationToken cancellation;

    // Koliko je redova vec predato aplikaciji - za napredak i za dijagnostiku.
    int deliveredLines = 0;

    // Da li je poslednji prolaz zatvoren IZRICITO, a ne tek mrezom u
    // destruktoru sesije.
    //
    // Oba puta zaustave cip, pa se spolja ne razlikuju - a razlikuju se u
    // onome sto znace: mreza je poslednja odbrana, ne redovan put. Bez ovog
    // polja nijedan test ne bi mogao reci koji je od dva radio, pa bi
    // izostanak izricitog zatvaranja prosao neprimeceno.
    bool closedExplicitly = false;

    bool valid() const noexcept { return signature == kItemContextSignature; }
    bool transferring() const noexcept { return session != nullptr; }

    // Prekini prenos i pusti sve sto drzi. Vraca ishod zatvaranja prolaza.
    Status endTransfer();
};

// Napravi i oslobodi. Par mora biti uravnotezen; WIA servis zove drugo kroz
// drvFreeDrvItemContext.
ItemContext* createItemContext(ItemKind kind);
void destroyItemContext(void* context) noexcept;

// Bezbedno tumacenje bloka koji je vratio WIA servis. Nullptr ako blok nije
// nas ili ne postoji.
ItemContext* asItemContext(void* raw) noexcept;

// Ponisti potpis, tako da asItemContext ovaj blok vise ne prepozna.
//
// Odvojen korak, a ne dve linije unutar destroyItemContext, iz jednog razloga:
// ovako se moze PROVERITI. Brisanje potpisa u trenutku oslobadjanja stiti od
// drugog poziva sa istim blokom - greske koju WIA servis zaista pravi - ali se
// ne moze testirati citanjem oslobodjene memorije, jer je i to greska.
void invalidateItemContext(ItemContext* context) noexcept;

}  // namespace g2710::wia
