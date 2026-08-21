#include "WiaItemContext.h"

#include <new>

namespace g2710::wia {

const char* toString(ItemKind kind) noexcept {
    switch (kind) {
        case ItemKind::Root:    return "Root";
        case ItemKind::Flatbed: return "Flatbed";
    }
    return "?";
}

Status ItemContext::endTransfer() {
    Status result = ok();
    closedExplicitly = false;

    if (session != nullptr) {
        // Uredno zatvaranje, ne samo brisanje: sesija mora spustiti bit
        // izvrsavanja, inace cip nastavlja da skenira posle poslednjeg reda
        // koji je iko procitao.
        //
        // Destruktor sesije to takodje uradi, ali kao MREZU - i tada se
        // greska nema kome prijaviti. Ovde se ima.
        result = session->finish();
        closedExplicitly = static_cast<bool>(result);
        session.reset();
    }

    registers.reset();
    cancellation.reset();
    deliveredLines = 0;
    return result;
}

ItemContext* createItemContext(ItemKind kind) {
    auto* context = new (std::nothrow) ItemContext();
    if (context == nullptr) {
        return nullptr;
    }
    context->kind = kind;
    if (kind == ItemKind::Flatbed) {
        context->settings = defaultSettings();
    }
    return context;
}

void destroyItemContext(void* raw) noexcept {
    ItemContext* context = asItemContext(raw);
    if (context == nullptr) {
        return;
    }
    (void)context->endTransfer();

    // Potpis se brise PRE oslobadjanja. Ako WIA servis isti blok posalje jos
    // jednom - a to je greska koja se desava - asItemContext ce ga odbiti
    // umesto da radi nad oslobodjenom memorijom.
    //
    // NIJEDAN TEST NE POKRIVA BAS OVU LINIJU, i to nije previd: da bi se
    // videla razlika, morao bi se procitati blok POSLE oslobadjanja - a to
    // je greska bez obzira na ishod. Sam korak se testira odvojeno
    // (invalidateItemContext), a ovaj poziv bi uhvatio tek adresni
    // sanitajzer nad dvostrukim oslobadjanjem.
    invalidateItemContext(context);
    delete context;
}

void invalidateItemContext(ItemContext* context) noexcept {
    if (context != nullptr) {
        context->signature = 0;
    }
}

ItemContext* asItemContext(void* raw) noexcept {
    if (raw == nullptr) {
        return nullptr;
    }
    auto* context = static_cast<ItemContext*>(raw);
    return context->valid() ? context : nullptr;
}

}  // namespace g2710::wia
