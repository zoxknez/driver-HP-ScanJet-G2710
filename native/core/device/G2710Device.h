// Zivotni ciklus uredjaja.
//
// Spaja transport, arbitrazu, stanje, poziciju glave i bezbednosnu kapiju u
// jedan objekat, tako da svaki klijent - CLI, aplikacija, TWAIN, WIA - prolazi
// kroz iste provere. Ono sto ovde radi, radi i tamo.
//
// Sta OVDE nije: kretanje i akvizicija. Kretanje ide kroz MotionGuard, kome
// pozivalac daje hookove; ova klasa mu obezbedjuje stanje i poziciju koje
// guard cuva. Kontrola lampe ceka G2710-5, kada se Lamp_Warmup ekstraktuje -
// pretpostavljati koji bit pali lampu na tudjem uredjaju nije nesto sto ovaj
// projekat sebi dozvoljava.

#pragma once

#include "../rts8822/Rts8822.h"
#include "../transport/ITransport.h"
#include "../transport/ITransportProvider.h"
#include "../util/Cancellation.h"
#include "../util/Clock.h"
#include "DeviceArbiter.h"
#include "DeviceState.h"
#include "HeadPosition.h"
#include "SafetyLevel.h"

#include <chrono>
#include <memory>
#include <string>

namespace g2710 {

struct DeviceOptions {
    SafetyGate safety{};

    // Ime koje se upisuje u arbitrazu, da sledeci pozivalac vidi ko drzi
    // uredjaj umesto sirovog Win32 koda.
    std::string clientName = "g2710";

    // Koliko cekati na ekskluzivnu sesiju pre nego sto se odustane.
    std::chrono::milliseconds acquireTimeout{5000};
};

class G2710Device {
public:
    // Otvara transport i pravi objekat. NE zauzima uredjaj i NE proverava
    // identitet - to su odvojeni koraci, jer prvi moze uspeti a drugi ne.
    static Result<std::unique_ptr<G2710Device>> open(const DeviceRef& ref,
                                                     DeviceOptions options = {});

    ~G2710Device();

    G2710Device(const G2710Device&) = delete;
    G2710Device& operator=(const G2710Device&) = delete;

    // Cita USB identitet i odbija sve sto nije G2710.
    //
    // \\.\Usbscan0 je DELJENO ime - na masini moze biti bilo koji uredjaj
    // vezan za usbscan.sys. Na razvojnoj masini je to bio HP LaserJet MFP
    // M139-M142, cija je skener funkcija takodje pod klasom Image. Vendor
    // komanda sa G2710 registarskom semantikom poslata tudjem uredjaju je
    // tacno ono sto ovaj projekat sebi zabranjuje.
    Status identify();

    // Zauzima ekskluzivnu sesiju i prelazi u Idle. Od ovog trenutka uredjaj
    // je nas i niko drugi ne sme da ga pomera.
    Status begin();

    // Oslobadja sesiju i vraca se u Identified.
    //
    // Vraca Status, a ne void: prvi pokusaj je tiho gutao odbijen prelaz sa
    // (void), pa je nedostajuci prelaz Idle -> Identified prosao neprimeceno
    // dok ga test nije nasao.
    Status end();

    // --- stanje ----------------------------------------------------------

    DeviceState state() const noexcept { return machine_.state(); }
    DeviceStateMachine& stateMachine() noexcept { return machine_; }

    const HeadPosition& headPosition() const noexcept { return position_; }
    HeadPosition& headPosition() noexcept { return position_; }

    const SafetyGate& safety() const noexcept { return options_.safety; }
    ITransport& transport() noexcept { return *transport_; }
    rts8822::Rts8822& chip() noexcept { return chip_; }

    const DeviceIdentity& identity() const noexcept { return identity_; }
    bool isIdentified() const noexcept { return identified_; }

    // Ko trenutno drzi uredjaj, ako se moze utvrditi.
    std::string currentOwner() const { return arbiter_.currentOwner(); }
    ArbiterScope arbiterScope() const noexcept { return arbiter_.scope(); }

    // --- citanja nivoa 1 --------------------------------------------------

    Result<bool> isHeadAtHome();
    Result<rts8822::LampStatus> lampStatus();

    // --- prekid i oporavak ------------------------------------------------

    CancellationToken& cancellation() noexcept { return token_; }

    // Prekida sve u letu. Bezbedno iz drugog thread-a.
    void cancel() noexcept;

    // Posle gubitka veze: ponovo otvara transport i vraca se u Opened.
    //
    // NE vraca poziciju glave. Posle ovoga je HOME obavezan - vidi
    // docs/SAFETY.md, 2.
    Status recoverFromTransportLoss();

private:
    G2710Device(std::unique_ptr<ITransport> transport, DeviceOptions options);

    // Prevodi gresku transporta u prelaz stanja. Gubitak veze mora oboriti i
    // stanje i poziciju, bez obzira ko ga je prvi primetio.
    void noteTransportError(const Error& error) noexcept;

    std::unique_ptr<ITransport> transport_;
    DeviceOptions options_;
    DeviceStateMachine machine_;
    HeadPosition position_;
    DeviceArbiter arbiter_;
    DataSession session_;
    CancellationToken token_;
    rts8822::Rts8822 chip_;
    DeviceIdentity identity_{};
    bool identified_ = false;
};

}  // namespace g2710
