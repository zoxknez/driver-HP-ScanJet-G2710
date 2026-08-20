#include "device/SafetyLevel.h"

#include <gtest/gtest.h>

using namespace g2710;

// Ovaj TU se gradi sa podrazumevanim plafonom (5). Ponasanje na spustenom
// plafonu proverava g2710_safety_ceiling1_test, koji se gradi zasebno - plafon
// mora biti jedna vrednost po binarnom fajlu, pa se ne moze mesati u istom.
static_assert(G2710_BUILD_SAFETY_CEILING == 5,
              "podrazumevani test build ocekuje plafon 5");

TEST(SafetyGate, EffectiveIsMinOfCeilingAndRequested) {
    const SafetyGate gate{SafetyLevel::Lamp};
    EXPECT_EQ(gate.requested(), SafetyLevel::Lamp);
    EXPECT_EQ(gate.effective(), SafetyLevel::Lamp);
    EXPECT_FALSE(gate.wasClamped());
}

TEST(SafetyGate, AllowsAtOrBelowEffective) {
    const SafetyGate gate{SafetyLevel::Motor};
    EXPECT_TRUE(gate.allows(SafetyLevel::ReadOnly));
    EXPECT_TRUE(gate.allows(SafetyLevel::Lamp));
    EXPECT_TRUE(gate.allows(SafetyLevel::Motor));
    EXPECT_FALSE(gate.allows(SafetyLevel::Acquire));
    EXPECT_FALSE(gate.allows(SafetyLevel::FullScan));
}

TEST(SafetyGate, RequireIsHardErrorNotWarning) {
    const SafetyGate gate{SafetyLevel::Lamp};

    const Status allowed = gate.require(SafetyLevel::Lamp, "lampOn");
    EXPECT_TRUE(allowed.hasValue());

    const Status refused = gate.require(SafetyLevel::Motor, "moveHome");
    ASSERT_FALSE(refused.hasValue());
    EXPECT_EQ(refused.error().code, ErrorCode::SafetyViolation);
    EXPECT_STREQ(refused.error().context, "moveHome");
}

TEST(SafetyGate, DefaultConstructedUsesCeiling) {
    const SafetyGate gate;
    EXPECT_EQ(gate.effective(), kBuildSafetyCeiling);
    EXPECT_FALSE(gate.wasClamped());
}

TEST(SafetyGate, RequestAboveCeilingIsClampedNotRejected) {
    // Zahtev iznad plafona je legitiman - spusta se tiho. Ono sto se odbija
    // je pokusaj IZVRSENJA operacije iznad efektivnog nivoa.
    const SafetyGate gate{static_cast<SafetyLevel>(99)};
    EXPECT_EQ(gate.effective(), kBuildSafetyCeiling);
    EXPECT_TRUE(gate.wasClamped());
}
