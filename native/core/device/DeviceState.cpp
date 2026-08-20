#include "DeviceState.h"

namespace g2710 {
namespace {

// Stanja u kojima uredjaj nesto aktivno radi.
bool isBusy(DeviceState state) noexcept {
    switch (state) {
        case DeviceState::WarmingUp:
        case DeviceState::Homing:
        case DeviceState::Calibrating:
        case DeviceState::Scanning:
            return true;
        default:
            return false;
    }
}

}  // namespace

const char* toString(DeviceState state) noexcept {
    switch (state) {
        case DeviceState::Disconnected:     return "Disconnected";
        case DeviceState::Opened:           return "Opened";
        case DeviceState::Identified:       return "Identified";
        case DeviceState::Idle:             return "Idle";
        case DeviceState::WarmingUp:        return "WarmingUp";
        case DeviceState::Homing:           return "Homing";
        case DeviceState::Calibrating:      return "Calibrating";
        case DeviceState::Scanning:         return "Scanning";
        case DeviceState::Cancelling:       return "Cancelling";
        case DeviceState::TransportLost:    return "TransportLost";
        case DeviceState::Faulted:          return "Faulted";
        case DeviceState::EmergencyStopped: return "EmergencyStopped";
    }
    return "?";
}

bool allowsMotion(DeviceState state) noexcept {
    // Namerno uska lista. Kretanje je dozvoljeno samo tamo gde znamo sta
    // radimo; sve ostalo je zabranjeno dok se ne oporavi.
    switch (state) {
        case DeviceState::Idle:
        case DeviceState::Homing:
        case DeviceState::Calibrating:
        case DeviceState::Scanning:
            return true;
        default:
            return false;
    }
}

bool requiresRecovery(DeviceState state) noexcept {
    switch (state) {
        case DeviceState::TransportLost:
        case DeviceState::Faulted:
        case DeviceState::EmergencyStopped:
            return true;
        default:
            return false;
    }
}

bool isTransitionAllowed(DeviceState from, DeviceState to) noexcept {
    if (from == to) {
        return true;
    }

    // Gubitak veze i greska mogu se dogoditi iz bilo cega.
    if (to == DeviceState::TransportLost || to == DeviceState::Faulted) {
        return true;
    }

    // Zaustavljanje u nuzdi je dozvoljeno svuda gde se uredjaj moze kretati,
    // i iz Cancelling-a.
    if (to == DeviceState::EmergencyStopped) {
        return allowsMotion(from) || from == DeviceState::Cancelling ||
               from == DeviceState::WarmingUp;
    }

    switch (from) {
        case DeviceState::Disconnected:
            return to == DeviceState::Opened;

        case DeviceState::Opened:
            // Zatvaranje je uvek moguce; identifikacija je jedini put napred.
            return to == DeviceState::Identified || to == DeviceState::Disconnected;

        case DeviceState::Identified:
            return to == DeviceState::Idle || to == DeviceState::Disconnected;

        case DeviceState::Idle:
            // Identified je povratak koji se lako previdi: klijent oslobadja
            // ekskluzivnu sesiju a ne zatvara uredjaj. Upravo to se desava
            // kada WIA zavrsi a TWAIN dolazi na red.
            return to == DeviceState::WarmingUp || to == DeviceState::Homing ||
                   to == DeviceState::Calibrating || to == DeviceState::Scanning ||
                   to == DeviceState::Identified || to == DeviceState::Disconnected;

        case DeviceState::WarmingUp:
        case DeviceState::Homing:
        case DeviceState::Calibrating:
        case DeviceState::Scanning:
            // Iz radnog stanja nazad u Idle, ili u otkazivanje.
            return to == DeviceState::Idle || to == DeviceState::Cancelling ||
                   isBusy(to);

        case DeviceState::Cancelling:
            return to == DeviceState::Idle;

        case DeviceState::TransportLost:
            // Oporavak ide iskljucivo preko ponovnog otvaranja. Povratak
            // pravo u Idle bi znacio da smo pretpostavili poziciju glave.
            return to == DeviceState::Disconnected || to == DeviceState::Opened;

        case DeviceState::Faulted:
            return to == DeviceState::Disconnected || to == DeviceState::Opened ||
                   to == DeviceState::Idle;

        case DeviceState::EmergencyStopped:
            // Posle zaustavljanja u nuzdi HOME je obavezan, pa se ide u
            // Homing ili se uredjaj zatvara.
            return to == DeviceState::Homing || to == DeviceState::Disconnected;
    }

    return false;
}

Status DeviceStateMachine::transitionTo(DeviceState next) noexcept {
    if (!isTransitionAllowed(state_, next)) {
        return fail(ErrorCode::InvalidState, "nedozvoljen prelaz stanja");
    }
    if (state_ != next) {
        ++transitions_;
    }
    state_ = next;
    return ok();
}

void DeviceStateMachine::onTransportLost() noexcept {
    if (state_ != DeviceState::TransportLost) {
        ++transitions_;
    }
    state_ = DeviceState::TransportLost;
}

void DeviceStateMachine::onFault() noexcept {
    if (state_ != DeviceState::Faulted) {
        ++transitions_;
    }
    state_ = DeviceState::Faulted;
}

}  // namespace g2710
