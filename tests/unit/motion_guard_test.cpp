// Stanje uredjaja, pozicija glave i zastita kretanja.
//
// Ovo je fault-injection matrica iz plana. Tvrdnja koju testovi treba da
// odbrane glasi:
//
//   softver garantuje da NIKADA nece nastaviti da izdaje kretanje;
//   NE garantuje da je motor stao.
//
// Razlika je sustinska i ovde se meri: kada veze nema, emergencyStopAttempted
// mora biti false. Prijaviti pokusaj zaustavljanja motora kome se ne moze
// pristupiti znacilo bi lagati izvestaj koji ce citati neko ko drzi skener u
// rukama.

#include "device/DeviceState.h"
#include "device/HeadPosition.h"
#include "device/MotionGuard.h"
#include "util/Cancellation.h"
#include "util/Clock.h"

#include <gtest/gtest.h>

#include <vector>

using namespace g2710;
using namespace std::chrono_literals;

// --- pozicija glave ----------------------------------------------------------

TEST(HeadPositionTest, StartsUnknownNotAtZero) {
    // Uredjaj koji je tek otvoren nije nuzno na home poziciji - prethodni
    // klijent je mogao pasti u sred scana.
    HeadPosition position;
    EXPECT_FALSE(position.isKnown());
}

TEST(HeadPositionTest, AdvanceDoesNothingWhileUnknown) {
    // Pomeranje nepoznate pozicije ne sme napraviti lazan podatak.
    HeadPosition position;
    position.advance(500);
    EXPECT_FALSE(position.isKnown());
}

TEST(HeadPositionTest, InvalidateCountsAndForgets) {
    HeadPosition position;
    position.setKnown(1234);
    ASSERT_TRUE(position.isKnown());

    position.invalidate();
    EXPECT_FALSE(position.isKnown());
    EXPECT_EQ(position.invalidationCount(), 1);
}

// --- prelazi stanja ----------------------------------------------------------

TEST(DeviceStateTest, MotionIsAllowedOnlyWhereWeKnowWhatWeAreDoing) {
    EXPECT_TRUE(allowsMotion(DeviceState::Idle));
    EXPECT_TRUE(allowsMotion(DeviceState::Homing));
    EXPECT_TRUE(allowsMotion(DeviceState::Scanning));

    EXPECT_FALSE(allowsMotion(DeviceState::Disconnected));
    EXPECT_FALSE(allowsMotion(DeviceState::Opened));
    EXPECT_FALSE(allowsMotion(DeviceState::TransportLost));
    EXPECT_FALSE(allowsMotion(DeviceState::Faulted));
    EXPECT_FALSE(allowsMotion(DeviceState::EmergencyStopped));
    EXPECT_FALSE(allowsMotion(DeviceState::Cancelling));
}

TEST(DeviceStateTest, TransportLossIsReachableFromEverywhere) {
    const DeviceState all[] = {
        DeviceState::Disconnected, DeviceState::Opened, DeviceState::Identified,
        DeviceState::Idle, DeviceState::WarmingUp, DeviceState::Homing,
        DeviceState::Calibrating, DeviceState::Scanning, DeviceState::Cancelling,
        DeviceState::Faulted, DeviceState::EmergencyStopped,
    };
    for (const DeviceState from : all) {
        EXPECT_TRUE(isTransitionAllowed(from, DeviceState::TransportLost))
            << "gubitak veze nije dozvoljen iz " << toString(from);
    }
}

TEST(DeviceStateTest, RecoveryFromTransportLossGoesThroughReopenNotStraightToIdle) {
    // Povratak pravo u Idle znacio bi da smo pretpostavili gde je glava.
    EXPECT_FALSE(isTransitionAllowed(DeviceState::TransportLost, DeviceState::Idle));
    EXPECT_TRUE(isTransitionAllowed(DeviceState::TransportLost, DeviceState::Opened));
}

TEST(DeviceStateTest, AfterEmergencyStopTheOnlyWayForwardIsHoming) {
    EXPECT_TRUE(isTransitionAllowed(DeviceState::EmergencyStopped, DeviceState::Homing));
    EXPECT_FALSE(isTransitionAllowed(DeviceState::EmergencyStopped, DeviceState::Idle));
    EXPECT_FALSE(isTransitionAllowed(DeviceState::EmergencyStopped, DeviceState::Scanning));
}

TEST(DeviceStateTest, IllegalTransitionIsAnErrorNotASilentFix) {
    DeviceStateMachine machine;
    const Status status = machine.transitionTo(DeviceState::Scanning);

    ASSERT_FALSE(status.hasValue());
    EXPECT_EQ(status.error().code, ErrorCode::InvalidState);
    EXPECT_EQ(machine.state(), DeviceState::Disconnected) << "stanje je ipak promenjeno";
}

TEST(DeviceStateTest, NormalStartupPath) {
    DeviceStateMachine machine;
    ASSERT_TRUE(machine.transitionTo(DeviceState::Opened).hasValue());
    ASSERT_TRUE(machine.transitionTo(DeviceState::Identified).hasValue());
    ASSERT_TRUE(machine.transitionTo(DeviceState::Idle).hasValue());
    EXPECT_TRUE(machine.allowsMotion());
}

// --- MotionGuard -------------------------------------------------------------

#if G2710_MOTOR_PATH_COMPILED

namespace {

// Sabira sta se dogodilo, da testovi mogu da tvrde i ono sto se NIJE desilo.
struct FakeHardware {
    int stepsRequested = 0;
    int stepsDelivered = 0;
    int emergencyStops = 0;
    bool transportAlive = true;
    bool atHome = false;

    // Koliko koraka motor stvarno pravi po pozivu; manje od trazenog je zastoj.
    int deliverPerCall = -1;

    // Greska koju step() vraca umesto koraka.
    ErrorCode stepError = ErrorCode::Ok;
    int failAfterCalls = -1;
    int stepCalls = 0;
};

MotionHooks makeHooks(FakeHardware& hw) {
    MotionHooks hooks;

    hooks.step = [&hw](int steps) -> Result<int> {
        ++hw.stepCalls;
        hw.stepsRequested += steps;

        if (hw.stepError != ErrorCode::Ok &&
            (hw.failAfterCalls < 0 || hw.stepCalls > hw.failAfterCalls)) {
            return fail(hw.stepError, "fake step");
        }

        const int done = hw.deliverPerCall < 0 ? steps : hw.deliverPerCall;
        hw.stepsDelivered += done;
        return done;
    };

    hooks.isAtHome = [&hw]() -> Result<bool> { return hw.atHome; };

    hooks.emergencyStop = [&hw]() -> Status {
        ++hw.emergencyStops;
        return ok();
    };

    hooks.isTransportAlive = [&hw]() { return hw.transportAlive; };
    return hooks;
}

MotionRequest simpleMove(int steps = 256) {
    MotionRequest request{};
    request.operation = "test.move";
    request.direction = MotionDirection::AwayFromHome;
    request.expectedSteps = steps;
    request.maximumSteps = steps * 2;
    request.deadline = 5000ms;
    return request;
}

class Motion : public ::testing::Test {
protected:
    DeviceStateMachine machine;
    HeadPosition position;
    ManualClock clock;
    CancellationToken token;
    FakeHardware hw;

    void SetUp() override {
        ASSERT_TRUE(machine.transitionTo(DeviceState::Opened).hasValue());
        ASSERT_TRUE(machine.transitionTo(DeviceState::Identified).hasValue());
        ASSERT_TRUE(machine.transitionTo(DeviceState::Idle).hasValue());
        position.setKnown(0);
    }

    MotionGuard guard() {
        return MotionGuard{machine, position, SafetyGate{SafetyLevel::FullScan}, clock};
    }
};

}  // namespace

// --- odbijanje zahteva pre nego sto se ista pomeri ---------------------------

TEST_F(Motion, RefusesMotionWithoutADeadline) {
    // Kretanje bez roka nad udaljenim uredjajem je upravo ono sto ovaj modul
    // postoji da sprecava.
    auto g = guard();
    MotionRequest request = simpleMove();
    request.deadline = 0ms;

    const auto result = g.run(request, makeHooks(hw), token);
    ASSERT_FALSE(result.hasValue());
    EXPECT_EQ(result.error().code, ErrorCode::InvalidArgument);
    EXPECT_EQ(hw.stepsRequested, 0) << "odbijen zahtev je ipak nesto pomerio";
}

TEST_F(Motion, RefusesWhenMaximumIsBelowExpected) {
    auto g = guard();
    MotionRequest request = simpleMove();
    request.maximumSteps = request.expectedSteps - 1;

    const auto result = g.run(request, makeHooks(hw), token);
    ASSERT_FALSE(result.hasValue());
    EXPECT_EQ(hw.stepsRequested, 0);
}

TEST_F(Motion, RefusesFromAStateThatDoesNotAllowMotion) {
    machine.onTransportLost();

    auto g = guard();
    const auto result = g.run(simpleMove(), makeHooks(hw), token);

    ASSERT_FALSE(result.hasValue());
    EXPECT_EQ(result.error().code, ErrorCode::InvalidState);
    EXPECT_EQ(hw.stepsRequested, 0)
        << "izdato je kretanje iz stanja u kome je zabranjeno";
}

TEST_F(Motion, RefusesBelowMotorSafetyLevel) {
    MotionGuard restricted{machine, position, SafetyGate{SafetyLevel::Lamp}, clock};
    const auto result = restricted.run(simpleMove(), makeHooks(hw), token);

    ASSERT_FALSE(result.hasValue());
    EXPECT_EQ(result.error().code, ErrorCode::SafetyViolation);
    EXPECT_EQ(hw.stepsRequested, 0);
}

// --- uspesno kretanje --------------------------------------------------------

TEST_F(Motion, CompletedMoveAdvancesTheKnownPosition) {
    auto g = guard();
    const auto result = g.run(simpleMove(256), makeHooks(hw), token);

    ASSERT_TRUE(result.hasValue());
    EXPECT_EQ(result.value().status, MotionStatus::Completed);
    EXPECT_EQ(result.value().stepsTaken, 256);
    EXPECT_TRUE(result.value().positionKnown);
    EXPECT_EQ(position.steps(), 256);
}

TEST_F(Motion, MovingTowardHomeDecreasesPosition) {
    position.setKnown(1000);
    auto g = guard();

    MotionRequest request = simpleMove(256);
    request.direction = MotionDirection::TowardHome;

    ASSERT_TRUE(g.run(request, makeHooks(hw), token).hasValue());
    EXPECT_EQ(position.steps(), 1000 - 256);
}

TEST_F(Motion, HomeSensorMakesPositionKnownWhenHomeIsTheGoal) {
    // HOME je jedina operacija posle koje pozicija postaje poznata bez
    // pretpostavki - senzor je rekao gde smo.
    position.invalidate();
    ASSERT_FALSE(position.isKnown());

    hw.atHome = true;
    auto g = guard();

    MotionRequest request = simpleMove(5000);
    request.direction = MotionDirection::TowardHome;
    request.homeIsGoal = true;

    const auto result = g.run(request, makeHooks(hw), token);
    ASSERT_TRUE(result.hasValue());
    EXPECT_EQ(result.value().status, MotionStatus::StoppedAtHome);
    EXPECT_TRUE(position.isKnown());
    EXPECT_EQ(position.steps(), 0);
}

TEST_F(Motion, UnexpectedHomeContactInvalidatesPosition) {
    // Ako smo naleteli na home a to nije bio cilj, nesto se razislo sa
    // stvarnoscu i poziciji se vise ne veruje.
    hw.atHome = true;
    auto g = guard();

    MotionRequest request = simpleMove(256);
    request.direction = MotionDirection::TowardHome;
    request.homeIsGoal = false;

    const auto result = g.run(request, makeHooks(hw), token);
    ASSERT_TRUE(result.hasValue());
    EXPECT_EQ(result.value().status, MotionStatus::StoppedAtHome);
    EXPECT_FALSE(position.isKnown());
}

// --- prekidi -----------------------------------------------------------------

TEST_F(Motion, CancellationStopsAndInvalidatesPosition) {
    token.cancel();
    auto g = guard();

    const auto result = g.run(simpleMove(), makeHooks(hw), token);
    ASSERT_TRUE(result.hasValue());

    EXPECT_EQ(result.value().status, MotionStatus::Cancelled);
    EXPECT_TRUE(result.value().emergencyStopAttempted);
    EXPECT_EQ(hw.emergencyStops, 1);
    EXPECT_FALSE(position.isKnown());
}

TEST_F(Motion, DeadlineStopsAndInvalidatesPosition) {
    auto g = guard();
    g.setStepChunk(1);

    // Sat se pomera na svaki korak; rok istekne usred kretanja.
    FakeHardware& hardware = hw;
    MotionHooks hooks = makeHooks(hardware);
    auto innerStep = hooks.step;
    hooks.step = [&](int steps) -> Result<int> {
        clock.advance(100ms);
        return innerStep(steps);
    };

    MotionRequest request = simpleMove(1000);
    request.deadline = 300ms;

    const auto result = g.run(request, hooks, token);
    ASSERT_TRUE(result.hasValue());

    EXPECT_EQ(result.value().status, MotionStatus::DeadlineExceeded);
    EXPECT_TRUE(result.value().emergencyStopAttempted);
    EXPECT_LT(result.value().stepsTaken, 1000);
    EXPECT_FALSE(position.isKnown());
}

TEST_F(Motion, StallIsDetectedWhenTheMotorDeliversFewerSteps) {
    hw.deliverPerCall = 3;  // trazi se vise nego sto stize
    auto g = guard();
    g.setStepChunk(64);

    const auto result = g.run(simpleMove(256), makeHooks(hw), token);
    ASSERT_TRUE(result.hasValue());

    EXPECT_EQ(result.value().status, MotionStatus::LimitExceeded);
    EXPECT_TRUE(result.value().emergencyStopAttempted);
    EXPECT_FALSE(position.isKnown()) << "posle zastoja pozicija nije pouzdana";
}

// --- NAJVAZNIJI DEO: gubitak veze --------------------------------------------

TEST_F(Motion, TransportLossDoesNotClaimAnAttemptedStop) {
    // Kada veze nema, STOP se ne moze poslati. Prijaviti ga kao pokusan
    // znacilo bi lagati izvestaj koji ce citati neko ko drzi skener u rukama.
    hw.stepError = ErrorCode::TransportLost;
    hw.transportAlive = false;

    auto g = guard();
    const auto result = g.run(simpleMove(), makeHooks(hw), token);
    ASSERT_TRUE(result.hasValue());

    EXPECT_EQ(result.value().status, MotionStatus::TransportLost);
    EXPECT_FALSE(result.value().emergencyStopAttempted)
        << "tvrdi se pokusaj zaustavljanja motora kome se ne moze pristupiti";
    EXPECT_EQ(hw.emergencyStops, 0);
}

TEST_F(Motion, CancelWithADeadTransportDoesNotClaimAnAttemptedStop) {
    // Ovo je scenario u kome provera veze STVARNO odlucuje.
    //
    // Na cistom TransportLost putu guard uopste ne stigne do pokusaja
    // zaustavljanja, pa test nad njim prolazi i kada je provera uklonjena -
    // sto sam i izmerio. Ovde je prekid CANCEL, dakle put koji zaustavljanje
    // pokusava, ali je kabl vec iscupan: korisnik je otkazao, a veze nema.
    //
    // Bez provere, MotionResult bi tvrdio pokusaj zaustavljanja motora kome
    // se ne moze pristupiti.
    token.cancel();
    hw.transportAlive = false;

    auto g = guard();
    const auto result = g.run(simpleMove(), makeHooks(hw), token);
    ASSERT_TRUE(result.hasValue());

    EXPECT_EQ(result.value().status, MotionStatus::Cancelled);
    EXPECT_FALSE(result.value().emergencyStopAttempted)
        << "tvrdi se pokusaj zaustavljanja iako veze nema";
    EXPECT_EQ(hw.emergencyStops, 0)
        << "STOP je poslat kroz mrtvu vezu";
    EXPECT_FALSE(position.isKnown());
}

TEST_F(Motion, DeadlineWithADeadTransportDoesNotClaimAnAttemptedStop) {
    hw.transportAlive = false;

    auto g = guard();
    g.setStepChunk(1);

    MotionHooks hooks = makeHooks(hw);
    auto innerStep = hooks.step;
    hooks.step = [&](int steps) -> Result<int> {
        clock.advance(100ms);
        return innerStep(steps);
    };

    MotionRequest request = simpleMove(1000);
    request.deadline = 200ms;

    const auto result = g.run(request, hooks, token);
    ASSERT_TRUE(result.hasValue());

    EXPECT_EQ(result.value().status, MotionStatus::DeadlineExceeded);
    EXPECT_FALSE(result.value().emergencyStopAttempted);
    EXPECT_EQ(hw.emergencyStops, 0);
}

TEST_F(Motion, TransportLossInvalidatesPositionAndState) {
    hw.stepError = ErrorCode::TransportLost;
    hw.transportAlive = false;

    auto g = guard();
    ASSERT_TRUE(g.run(simpleMove(), makeHooks(hw), token).hasValue());

    EXPECT_EQ(machine.state(), DeviceState::TransportLost);
    EXPECT_FALSE(position.isKnown());
    EXPECT_EQ(position.invalidationCount(), 1);
}

TEST_F(Motion, NoFurtherMotionIsIssuedAfterTransportLoss) {
    // Ovo je invariant koji stiti tudji uredjaj: posle gubitka veze nijedna
    // dalja komanda kretanja se ne izdaje.
    hw.stepError = ErrorCode::TransportLost;
    hw.transportAlive = false;

    auto g = guard();
    ASSERT_TRUE(g.run(simpleMove(), makeHooks(hw), token).hasValue());

    const int requestedBefore = hw.stepsRequested;

    // Drugi pokusaj mora biti odbijen JOS U PROVERI, bez ijednog koraka.
    const auto second = g.run(simpleMove(), makeHooks(hw), token);
    ASSERT_FALSE(second.hasValue());
    EXPECT_EQ(second.error().code, ErrorCode::InvalidState);
    EXPECT_EQ(hw.stepsRequested, requestedBefore);
}

TEST_F(Motion, HomeIsRequiredAfterReconnect) {
    // Posle ponovnog otvaranja pozicija je i dalje nepoznata; jedina
    // operacija koja je vraca je HOME.
    hw.stepError = ErrorCode::TransportLost;
    hw.transportAlive = false;

    auto g = guard();
    ASSERT_TRUE(g.run(simpleMove(), makeHooks(hw), token).hasValue());
    ASSERT_FALSE(position.isKnown());

    ASSERT_TRUE(machine.transitionTo(DeviceState::Opened).hasValue());
    ASSERT_TRUE(machine.transitionTo(DeviceState::Identified).hasValue());
    ASSERT_TRUE(machine.transitionTo(DeviceState::Idle).hasValue());

    EXPECT_FALSE(position.isKnown())
        << "ponovno otvaranje ne sme samo po sebi vratiti poziciju";

    // HOME je vraca.
    FakeHardware fresh;
    fresh.atHome = true;
    MotionRequest home = simpleMove(5000);
    home.direction = MotionDirection::TowardHome;
    home.homeIsGoal = true;

    ASSERT_TRUE(g.run(home, makeHooks(fresh), token).hasValue());
    EXPECT_TRUE(position.isKnown());
}

// --- matrica: nijedan ishod ne ostavlja kretanje u toku ----------------------

TEST_F(Motion, EveryInterruptionEndsInADefinedStateWithPositionInvalidated) {
    struct Scenario {
        const char* name;
        bool cancel;
        ErrorCode stepError;
        bool transportAlive;
        int deliverPerCall;
        MotionStatus expected;
        bool expectStopAttempt;
    };

    const Scenario scenarios[] = {
        {"cancel",     true,  ErrorCode::Ok,            true,  -1, MotionStatus::Cancelled,     true},
        {"stall",      false, ErrorCode::Ok,            true,   2, MotionStatus::LimitExceeded, true},
        {"usb lost",   false, ErrorCode::TransportLost, false, -1, MotionStatus::TransportLost, false},
    };

    for (const Scenario& scenario : scenarios) {
        DeviceStateMachine localMachine;
        HeadPosition localPosition;
        ManualClock localClock;
        CancellationToken localToken;
        FakeHardware localHw;

        ASSERT_TRUE(localMachine.transitionTo(DeviceState::Opened).hasValue());
        ASSERT_TRUE(localMachine.transitionTo(DeviceState::Identified).hasValue());
        ASSERT_TRUE(localMachine.transitionTo(DeviceState::Idle).hasValue());
        localPosition.setKnown(0);

        if (scenario.cancel) {
            localToken.cancel();
        }
        localHw.stepError = scenario.stepError;
        localHw.transportAlive = scenario.transportAlive;
        localHw.deliverPerCall = scenario.deliverPerCall;

        MotionGuard localGuard{localMachine, localPosition,
                               SafetyGate{SafetyLevel::FullScan}, localClock};
        const auto result = localGuard.run(simpleMove(), makeHooks(localHw), localToken);

        ASSERT_TRUE(result.hasValue()) << scenario.name;
        EXPECT_EQ(result.value().status, scenario.expected) << scenario.name;
        EXPECT_EQ(result.value().emergencyStopAttempted, scenario.expectStopAttempt)
            << scenario.name;
        EXPECT_FALSE(localPosition.isKnown())
            << scenario.name << ": pozicija je ostala 'poznata' posle prekida";
        EXPECT_FALSE(result.value().positionKnown) << scenario.name;
    }
}

#endif  // G2710_MOTOR_PATH_COMPILED
