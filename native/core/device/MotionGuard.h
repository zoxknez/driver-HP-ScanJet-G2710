// Zastita svake motorne operacije.
//
// Iz plana, doslovno: nijedna motorna operacija bez Direction, ExpectedSteps,
// MaximumSteps, StartPosition, ExpectedEndPosition, Deadline,
// CancellationToken, OnUsbLost i EmergencyStop.
//
// Zato MotionRequest nema podrazumevane vrednosti za ono sto stiti. Pozivalac
// koji "samo hoce da pomeri glavu" mora da kaze koliko najvise i do kada -
// jer je alternativa `while (!home) step();` nad tudjim uredjajem.
//
// CEO MODUL NESTAJE iz build-a sa BuildSafetyCeiling < 3.

#pragma once

#include "../util/Cancellation.h"
#include "../util/Clock.h"
#include "../util/Result.h"
#include "DeviceState.h"
#include "HeadPosition.h"
#include "SafetyLevel.h"

#include <functional>

namespace g2710 {

enum class MotionDirection {
    TowardHome,
    AwayFromHome,
};

const char* toString(MotionDirection direction) noexcept;

// Kako se kretanje zavrsilo.
enum class MotionStatus {
    Completed,        // presao ocekivani broj koraka
    StoppedAtHome,    // home senzor prekinuo kretanje, sto je za HOME uspeh
    Cancelled,        // otkazano tokenom
    DeadlineExceeded, // istekao rok
    LimitExceeded,    // dostignut MaximumSteps a cilj nije ispunjen
    TransportLost,    // veza nestala usred kretanja
    Refused,          // zahtev odbijen pre nego sto se ista pomerilo
};

const char* toString(MotionStatus status) noexcept;

struct MotionResult {
    MotionStatus status = MotionStatus::Refused;
    int stepsTaken = 0;

    // Da li je EmergencyStop uopste POKUSAN. Kada veza nestane, nije - i to
    // se ne sme predstaviti kao da jeste. Vidi docs/SAFETY.md, 2.
    bool emergencyStopAttempted = false;

    // Ishod tog pokusaja, ako ga je bilo.
    bool emergencyStopSucceeded = false;

    // Da li je pozicija glave posle ovoga poznata.
    bool positionKnown = false;
};

struct MotionRequest {
    // Ime operacije, za dijagnostiku. Bez podrazumevane vrednosti namerno.
    const char* operation;

    MotionDirection direction;

    // Koliko koraka operacija OCEKUJE da napravi.
    int expectedSteps;

    // Tvrda granica. Kretanje se prekida i kada nije stiglo do cilja.
    // Mora biti >= expectedSteps, inace je zahtev besmislen.
    int maximumSteps;

    // Rok. Nula znaci "bez roka" i NIJE dozvoljena - operacija bez roka nad
    // udaljenim uredjajem je upravo ono sto ovaj modul sprecava.
    Duration deadline;

    // Ako je true, dolazak na home senzor je uspeh, ne prekid.
    bool homeIsGoal = false;
};

// Sve sto MotionGuard treba od hardvera. Prosledjuje se spolja da bi se guard
// mogao testirati bez transporta.
struct MotionHooks {
    // Napravi najvise `steps` koraka; vrati koliko ih je stvarno napravljeno.
    std::function<Result<int>(int steps)> step;

    // Da li je glava na home senzoru.
    std::function<Result<bool>()> isAtHome;

    // Pokusaj hitnog zaustavljanja. Zove se SAMO ako transport postoji.
    std::function<Status()> emergencyStop;

    // Da li veza jos postoji. Guard ovo pita pre svakog pokusaja
    // zaustavljanja, da ne bi tvrdio da je stao motor kome ne moze da pristupi.
    std::function<bool()> isTransportAlive;
};

#if G2710_MOTOR_PATH_COMPILED

class MotionGuard {
public:
    MotionGuard(DeviceStateMachine& machine, HeadPosition& position,
                SafetyGate gate, Clock& clock = systemClock())
        : machine_(machine), position_(position), gate_(gate), clock_(clock) {}

    // Izvrsi kretanje pod zastitom. Nikada ne baca; svaki ishod je u
    // MotionResult ili u gresci.
    Result<MotionResult> run(const MotionRequest& request, const MotionHooks& hooks,
                             const CancellationToken& token);

    // Koliko koraka se pravi po jednoj iteraciji. Manje znaci finiju kontrolu
    // roka i otkazivanja, po cenu vise poziva.
    void setStepChunk(int steps) noexcept;
    int stepChunk() const noexcept { return stepChunk_; }

private:
    Result<MotionResult> refuse(const char* reason) const;
    void attemptEmergencyStop(const MotionHooks& hooks, MotionResult& result) const;

    DeviceStateMachine& machine_;
    HeadPosition& position_;
    SafetyGate gate_;
    Clock& clock_;
    int stepChunk_ = 64;
};

#endif  // G2710_MOTOR_PATH_COMPILED

}  // namespace g2710
