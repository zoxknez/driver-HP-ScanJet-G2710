// Stanje uredjaja.
//
// Prelazi su nabrojani eksplicitno, a ne implicitni u toku koda. Razlog je
// isti kao i za sve ostalo u ovoj fazi: uredjaj nije kod nas, pa se pogresan
// prelaz ne moze primetiti gledanjem.
//
// Dva stanja koja se lako pomesaju:
//
//   Disconnected   nikada nismo bili povezani, ili smo uredno zatvorili
//   TransportLost  veza je nestala USRED rada - pozicija glave je nepoznata,
//                  HOME je obavezan pre bilo cega drugog

#pragma once

#include "../util/Result.h"

namespace g2710 {

enum class DeviceState {
    Disconnected,
    Opened,
    Identified,
    Idle,
    WarmingUp,
    Homing,
    Calibrating,
    Scanning,
    Cancelling,
    TransportLost,
    Faulted,
    EmergencyStopped,
};

const char* toString(DeviceState state) noexcept;

// Da li je prelaz dozvoljen.
bool isTransitionAllowed(DeviceState from, DeviceState to) noexcept;

// Da li se u ovom stanju sme izdati komanda kretanja.
//
// Namerno je uska: kretanje je dozvoljeno samo iz stanja u kojima znamo sta
// radimo. TransportLost, Faulted i EmergencyStopped nisu medju njima, i to je
// invariant koji stiti udaljeni uredjaj.
bool allowsMotion(DeviceState state) noexcept;

// Da li je stanje terminalno u smislu da trazi eksplicitan oporavak.
bool requiresRecovery(DeviceState state) noexcept;

class DeviceStateMachine {
public:
    DeviceState state() const noexcept { return state_; }

    // Vraca InvalidState ako prelaz nije dozvoljen. Ne "popravlja" prelaz i
    // ne loguje pa nastavlja - pogresan prelaz je greska u pozivaocu.
    Status transitionTo(DeviceState next) noexcept;

    // Gubitak veze moze da se dogodi iz BILO KOG stanja i uvek je dozvoljen.
    // Zato ne ide kroz transitionTo.
    void onTransportLost() noexcept;

    // Prelaz u Faulted je takodje uvek dozvoljen - greska ne trazi dozvolu.
    void onFault() noexcept;

    bool allowsMotion() const noexcept { return g2710::allowsMotion(state_); }

    // Koliko je prelaza izvrseno; dijagnostika.
    int transitionCount() const noexcept { return transitions_; }

private:
    DeviceState state_ = DeviceState::Disconnected;
    int transitions_ = 0;
};

}  // namespace g2710
