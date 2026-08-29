#include "G2710Device.h"

#include "G2710Profile.generated.h"

#include <windows.h>

#include <string>
#include <string_view>

namespace g2710 {
namespace {

// Id procesa. Izdvojeno da se <windows.h> ne bi prosirivao kroz zaglavlje.
unsigned long currentProcessId() noexcept { return ::GetCurrentProcessId(); }

// Kljuc arbitraze mora biti stabilan za dati UREDJAJ. Isti za sve klijente,
// inace bi svaki zakljucao svoj objekat i svi bi mislili da poseduju skener.
//
// Ali simulator NIJE deljeni skener. Fizicki uredjaj je jedan i o njega se
// zaista otimaju WIA servis, TWAIN i aplikacija; simulator postoji unutar
// procesa koji ga je napravio, i dva procesa koja ga koriste ne dele nista.
//
// Prvo je i simulator dobijao isti kljuc. Posledica: dva test projekta
// pokrenuta paralelno otimala su se o istu bravu, pa je ceo paket testova pao
// na istek roka - iako svaki projekat sam prolazi. Ista greska bi pogodila i
// coveka koji drzi otvorenu aplikaciju u rezimu simulatora dok pokrece wizard.
//
// Mehanizam ostaje ISTI - isti Global\ objekat, isti kod - menja se samo ime
// uredjaja, sto je tacno ono cemu kljuc i sluzi.
std::string arbiterKeyForG2710(const ITransport& transport) {
    const char* name = transport.name();
    if (name != nullptr && std::string_view(name) == "sim") {
        return "03F0-2805-sim-" + std::to_string(currentProcessId());
    }
    return "03F0-2805";
}

}  // namespace

G2710Device::G2710Device(std::unique_ptr<ITransport> transport, DeviceOptions options)
    : transport_(std::move(transport)),
      options_(std::move(options)),
      arbiter_(arbiterKeyForG2710(*transport_)),
      chip_(*transport_, options_.safety) {}

G2710Device::~G2710Device() {
    // Destruktor ne moze prijaviti gresku, ali sesija MORA biti oslobodjena -
    // brava koja procuri blokira svakog sledeceg klijenta.
    (void)end();
}

Result<std::unique_ptr<G2710Device>> G2710Device::open(const DeviceRef& ref,
                                                       DeviceOptions options) {
    auto transport = TransportProvider::create(ref);
    if (!transport) {
        return transport.error();
    }
    return openWith(std::move(transport).value(), std::move(options));
}

Result<std::unique_ptr<G2710Device>> G2710Device::openWith(
    std::unique_ptr<ITransport> transport, DeviceOptions options) {
    if (transport == nullptr) {
        return fail(ErrorCode::InvalidArgument, "openWith(nullptr)");
    }

    std::unique_ptr<G2710Device> device(
        new G2710Device(std::move(transport), std::move(options)));

    if (const Status s = device->arbiter_.initialize(); !s) {
        return s.error();
    }
    if (const Status s = device->machine_.transitionTo(DeviceState::Opened); !s) {
        return s.error();
    }
    return device;
}

void G2710Device::noteTransportError(const Error& error) noexcept {
    if (error.code == ErrorCode::TransportLost) {
        machine_.onTransportLost();
        position_.invalidate();
    }
}

Status G2710Device::identify() {
    if (const Status allowed = options_.safety.require(SafetyLevel::ReadOnly, "identify");
        !allowed) {
        return allowed;
    }

    auto reported = transport_->identity();
    if (!reported) {
        noteTransportError(reported.error());
        return reported.error();
    }

    identity_ = reported.value();

    if (identity_.vendorId != profile::kUsbVendorId ||
        identity_.productId != profile::kUsbProductId) {
        // Ne prelazimo u Identified i ne dozvoljavamo nista dalje. Vendor
        // komande ne idu uredjaju koji nije nas.
        identified_ = false;
        return fail(ErrorCode::DeviceNotFound, "identify: uredjaj nije G2710");
    }

    identified_ = true;
    return machine_.transitionTo(DeviceState::Identified);
}

Status G2710Device::begin() {
    if (!identified_) {
        // Zauzimanje uredjaja koji nije potvrdjen kao G2710 bi znacilo da
        // sledeca komanda ide u nepoznato.
        return fail(ErrorCode::InvalidState, "begin: identify() nije prosao");
    }
    if (session_.held()) {
        return ok();
    }

    auto session = arbiter_.acquireData(options_.acquireTimeout, options_.clientName.c_str());
    if (!session) {
        return session.error();
    }
    session_ = std::move(session).value();

    return machine_.transitionTo(DeviceState::Idle);
}

Status G2710Device::end() {
    session_.release();
    if (machine_.state() == DeviceState::Idle) {
        return machine_.transitionTo(DeviceState::Identified);
    }
    return ok();
}

Result<bool> G2710Device::isHeadAtHome() {
    auto result = chip_.isHeadAtHome();
    if (!result) {
        noteTransportError(result.error());
    }
    return result;
}

Result<rts8822::LampStatus> G2710Device::lampStatus() {
    auto result = chip_.lampStatus();
    if (!result) {
        noteTransportError(result.error());
    }
    return result;
}

void G2710Device::cancel() noexcept {
    token_.cancel();
    transport_->cancel();
}

void G2710Device::endCancellation() noexcept {
    token_.reset();
    transport_->clearCancel();
}

Status G2710Device::recoverFromTransportLoss() {
    if (machine_.state() != DeviceState::TransportLost) {
        return fail(ErrorCode::InvalidState, "recover: veza nije izgubljena");
    }

    // Sesija se oslobadja pre ponovnog otvaranja - brava koja bi ostala
    // zauzeta blokirala bi i nas sopstveni oporavak.
    session_.release();

    if (const Status s = transport_->reopen(); !s) {
        return s;
    }

    token_.reset();
    identified_ = false;

    // Namerno se NE vraca pozicija glave. Ponovna veza ne govori nista o tome
    // gde je glava stala; HOME je obavezan. Vidi docs/SAFETY.md, 2.
    if (position_.isKnown()) {
        position_.invalidate();
    }

    return machine_.transitionTo(DeviceState::Opened);
}

}  // namespace g2710
