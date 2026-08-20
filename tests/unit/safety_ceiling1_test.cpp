// Dokazuje invariant koji stiti udaljeni hardver:
//
//   build sa plafonom 1 odbija SVE pozive nivoa 2-5, bez obzira sta CLI ili
//   config traze, i motorni kod u njemu nije ni kompajliran.
//
// Plafon mora biti jedna vrednost po binarnom fajlu, pa se ovaj TU gradi kao
// zaseban izvrsni fajl sa G2710_BUILD_SAFETY_CEILING=1 i sopstvenom kopijom
// SafetyLevel.cpp - mesanje razlicitih plafona u istom binarnom fajlu bi bilo
// ODR krsenje i dalo bi lazno prolazan test.

#include "device/SafetyLevel.h"

#include <cstdio>

using namespace g2710;

static_assert(G2710_BUILD_SAFETY_CEILING == 1, "ovaj TU se gradi sa plafonom 1");
static_assert(kBuildSafetyCeiling == SafetyLevel::ReadOnly, "plafon je ReadOnly");

// Motor path se ne kompajlira - nije runtime provera nego odsustvo koda.
static_assert(G2710_MOTOR_PATH_COMPILED == 0,
              "plafon 1 ne sme kompajlirati motorni kod");

// CLI koji trazi 5 dobija 1.
constexpr SafetyGate kMaxRequest{SafetyLevel::FullScan};
static_assert(kMaxRequest.effective() == SafetyLevel::ReadOnly,
              "zahtev 5 mora biti spusten na 1");
static_assert(kMaxRequest.wasClamped(), "spustanje mora biti vidljivo");

static_assert(kMaxRequest.allows(SafetyLevel::ReadOnly), "nivo 1 je dozvoljen");
static_assert(!kMaxRequest.allows(SafetyLevel::Lamp), "nivo 2 mora biti odbijen");
static_assert(!kMaxRequest.allows(SafetyLevel::Motor), "nivo 3 mora biti odbijen");
static_assert(!kMaxRequest.allows(SafetyLevel::Acquire), "nivo 4 mora biti odbijen");
static_assert(!kMaxRequest.allows(SafetyLevel::FullScan), "nivo 5 mora biti odbijen");

int main() {
    int failures = 0;

    const SafetyGate gate{SafetyLevel::FullScan};

    if (gate.require(SafetyLevel::ReadOnly, "readRegister").hasValue() == false) {
        std::printf("FAIL: nivo 1 je odbijen na plafonu 1\n");
        ++failures;
    }

    const SafetyLevel refused[] = {SafetyLevel::Lamp, SafetyLevel::Motor,
                                   SafetyLevel::Acquire, SafetyLevel::FullScan};
    for (const SafetyLevel level : refused) {
        const Status status = gate.require(level, "op");
        if (status.hasValue()) {
            std::printf("FAIL: nivo %s je prosao na plafonu 1\n", toString(level));
            ++failures;
        } else if (status.error().code != ErrorCode::SafetyViolation) {
            std::printf("FAIL: nivo %s je vratio %s umesto SafetyViolation\n",
                        toString(level), toString(status.error().code));
            ++failures;
        }
    }

    if (failures == 0) {
        std::printf("plafon 1: svi nivoi 2-5 odbijeni, motor path nije kompajliran\n");
    }
    return failures == 0 ? 0 : 1;
}
