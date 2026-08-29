// Kvalifikacione provere.
//
// Acceptance gate faze G2710-11 je da se cela isporuka proba na OVOJ masini,
// bez uredjaja. Ovi testovi su ta proba: kvalifikacija se pokrece nad
// simulatorom i proverava se da izvestaj kaze istinu.
//
// Najvaznije sto se ovde drzi zakljucanim nije koliko provera prodje, nego da
// se provere iznad plafona NE POKUSAVAJU. Paket koji ide prijatelju sa
// plafonom 1 ne sme ni da pokusa da upali lampu, a kamoli da pomeri motor.

#include "Qualification.h"

#include "SimTransport.h"
#include "scan/Capabilities.h"
#include "transport/ITransportProvider.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <vector>

using namespace g2710;
using namespace g2710::cli;

namespace {

class QualificationTest : public ::testing::Test {
protected:
    // Uredjaj otvoren preko simulatora, na zadatom nivou.
    //
    // Stari provider se PRVO rusi, pa se tek onda pravi novi. Dodela u
    // mestu radi obrnuto - napravi novi, pa unisti stari - a unistavanje
    // starog ScopedTestProvider-a VRACA produkcioni transport. Sledeci
    // open() bi tada otvorio pravi \\\\.\\Usbscan0.
    std::unique_ptr<G2710Device> open(SafetyLevel level) {
        provider_.reset();
        provider_ = std::make_unique<TransportProvider::ScopedTestProvider>(
            std::make_unique<sim::SimTransportProvider>());

        DeviceOptions options;
        options.safety = SafetyGate{level};
        options.clientName = "qualification-test";

        auto device = G2710Device::open(DeviceRef::defaultUsbScan(), options);
        EXPECT_TRUE(device);
        if (!device) {
            return nullptr;
        }
        EXPECT_TRUE(device.value()->identify());
        EXPECT_TRUE(device.value()->begin());
        return std::move(device.value());
    }

    void TearDown() override { provider_.reset(); }

    static const CheckResult* find(const std::vector<CheckResult>& results,
                                   const std::string& id) {
        const auto match = std::find_if(results.begin(), results.end(),
                                        [&](const CheckResult& r) { return r.id == id; });
        return match == results.end() ? nullptr : &*match;
    }

    static int countWith(const std::vector<CheckResult>& results, CheckOutcome outcome) {
        return static_cast<int>(std::count_if(
            results.begin(), results.end(),
            [&](const CheckResult& r) { return r.outcome == outcome; }));
    }

private:
    std::unique_ptr<TransportProvider::ScopedTestProvider> provider_;
};

}  // namespace

// --- pun nivo ------------------------------------------------------------------

TEST_F(QualificationTest, EverythingRunnablePassesAgainstTheSimulator) {
    auto device = open(SafetyLevel::FullScan);
    ASSERT_NE(device, nullptr);

    const std::vector<CheckResult> results = runQualification(*device);
    const QualificationSummary summary = summarise(results);

    EXPECT_GT(summary.passed, 0);
    EXPECT_EQ(summary.failed, 0) << "kvalifikacija pada na simulatoru";
    EXPECT_EQ(summary.total(), static_cast<int>(results.size()));
    EXPECT_TRUE(summary.clean());
}

// Svaka rezolucija koju kod ume da izvrsi mora imati svoju proveru. Bez toga
// bi 1200 i 2400 prosli kroz kvalifikaciju neprimeceni.
TEST_F(QualificationTest, EveryExecutableResolutionIsChecked) {
    auto device = open(SafetyLevel::FullScan);
    ASSERT_NE(device, nullptr);

    const std::vector<CheckResult> results = runQualification(*device);

    for (int dpi : scan::executableResolutions()) {
        const std::string id = "H8." + std::to_string(dpi);
        const CheckResult* check = find(results, id);
        ASSERT_NE(check, nullptr) << dpi << " dpi nema svoju proveru";
        EXPECT_EQ(check->outcome, CheckOutcome::Pass) << id << ": " << check->detail;
    }
}

// Kvar koji je kvalifikacija zaista nasla: 1200 i 2400 su isporucivali NULA
// redova. Test to drzi zakljucanim na mestu gde je i otkriven.
TEST_F(QualificationTest, HighResolutionsDeliverLinesNotNothing) {
    auto device = open(SafetyLevel::FullScan);
    ASSERT_NE(device, nullptr);

    const std::vector<CheckResult> results = runQualification(*device);

    for (const char* id : {"H8.1200", "H8.2400"}) {
        const CheckResult* check = find(results, id);
        ASSERT_NE(check, nullptr) << id;
        EXPECT_EQ(check->outcome, CheckOutcome::Pass) << id << ": " << check->detail;
        EXPECT_EQ(check->detail.find("0 lines"), std::string::npos)
            << id << " isporucuje nista: " << check->detail;
        EXPECT_NE(check->detail.find("software alignment"), std::string::npos)
            << id << " bi trebalo da ide softverskim poravnanjem";
    }
}

// --- plafon --------------------------------------------------------------------

// Ovo je razlog zasto modul postoji. Paket sa plafonom 1 ne sme ni da pokusa.
TEST_F(QualificationTest, ReadOnlyLevelNeverTouchesTheLamp) {
    auto device = open(SafetyLevel::ReadOnly);
    ASSERT_NE(device, nullptr);

    const std::vector<CheckResult> results = runQualification(*device);

    for (const char* id : {"H3.1", "H3.2", "H3.3"}) {
        const CheckResult* check = find(results, id);
        ASSERT_NE(check, nullptr) << id;
        EXPECT_EQ(check->outcome, CheckOutcome::BlockedBySafetyLevel) << id;
        EXPECT_NE(check->detail.find("level"), std::string::npos)
            << "razlog mora navesti nivo: " << check->detail;
    }

    // Lampa je zaista ostala ugasena.
    const auto lamp = device->lampStatus();
    ASSERT_TRUE(lamp);
    EXPECT_FALSE(lamp.value().flatbedOn) << "lampa je upaljena uprkos plafonu";
}

TEST_F(QualificationTest, ReadOnlyLevelStillChecksWhatItCan) {
    auto device = open(SafetyLevel::ReadOnly);
    ASSERT_NE(device, nullptr);

    const std::vector<CheckResult> results = runQualification(*device);

    // Enumeracija i citanje registara su nivo 1 i moraju proci.
    for (const char* id : {"H1.1", "H1.2", "H2.1", "H2.2"}) {
        const CheckResult* check = find(results, id);
        ASSERT_NE(check, nullptr) << id;
        EXPECT_EQ(check->outcome, CheckOutcome::Pass) << id << ": " << check->detail;
    }

    EXPECT_EQ(countWith(results, CheckOutcome::Fail), 0)
        << "nizi plafon ne sme praviti PADOVE, samo blokade";
    EXPECT_GT(countWith(results, CheckOutcome::BlockedBySafetyLevel), 0);
}

TEST_F(QualificationTest, EachLevelUnblocksMoreThanThePreviousOne) {
    int previousBlocked = -1;

    for (SafetyLevel level : {SafetyLevel::ReadOnly, SafetyLevel::Lamp, SafetyLevel::Motor,
                              SafetyLevel::FullScan}) {
        auto device = open(level);
        ASSERT_NE(device, nullptr) << toInt(level);

        const std::vector<CheckResult> results = runQualification(*device);
        const int blocked = countWith(results, CheckOutcome::BlockedBySafetyLevel);

        // Uredjaj mora otici PRE nego sto sledeci prolaz zameni transport -
        // inace bi drzao pokazivac na provider koji vise ne postoji.
        device.reset();

        if (previousBlocked >= 0) {
            EXPECT_LE(blocked, previousBlocked)
                << "nivo " << toInt(level) << " blokira vise nego prethodni";
        }
        previousBlocked = blocked;
        EXPECT_EQ(countWith(results, CheckOutcome::Fail), 0) << "nivo " << toInt(level);
    }
}

// --- ono sto masina ne moze ------------------------------------------------------

// Da li lampa svetli ne moze se procitati iz registra. Provera koja bi to
// tvrdila lagala bi.
TEST_F(QualificationTest, QuestionsForTheUserCarryATextToShow) {
    auto device = open(SafetyLevel::FullScan);
    ASSERT_NE(device, nullptr);

    const std::vector<CheckResult> results = runQualification(*device);
    int questions = 0;

    for (const CheckResult& result : results) {
        if (result.outcome != CheckOutcome::AsksTheUser) {
            continue;
        }
        ++questions;
        EXPECT_FALSE(result.question.empty()) << result.id << " nema sta da pita";
        EXPECT_NE(result.question.find('?'), std::string::npos)
            << result.id << ": pitanje ne izgleda kao pitanje";
    }
    EXPECT_GT(questions, 0) << "bar lampa i dugmad moraju traziti oko";
}

// Provera koja ceka kod mora reci STA ceka. "nije uspelo" nije izvestaj.
TEST_F(QualificationTest, PendingChecksNameWhatTheyAreWaitingFor) {
    auto device = open(SafetyLevel::FullScan);
    ASSERT_NE(device, nullptr);

    const std::vector<CheckResult> results = runQualification(*device);

    for (const CheckResult& result : results) {
        if (result.outcome == CheckOutcome::NotImplemented) {
            EXPECT_FALSE(result.detail.empty()) << result.id << " ne kaze sta ceka";
            EXPECT_GT(result.detail.size(), 10u) << result.id << ": " << result.detail;
        }
    }

    const CheckResult* motion = find(results, "H4.1");
    ASSERT_NE(motion, nullptr);
    EXPECT_EQ(motion->outcome, CheckOutcome::NotImplemented);
    EXPECT_NE(motion->detail.find("Head_Relocate"), std::string::npos)
        << "razlog mora imenovati sta nedostaje";
}

// --- izvestaj --------------------------------------------------------------------

TEST_F(QualificationTest, ReportContainsEveryCheck) {
    auto device = open(SafetyLevel::FullScan);
    ASSERT_NE(device, nullptr);

    const std::vector<CheckResult> results = runQualification(*device);
    const std::string report =
        formatReport(results, "03F0-2805", "2026-08-21T10:00:00", device->safety());

    for (const CheckResult& result : results) {
        EXPECT_NE(report.find("\"" + result.id + "\""), std::string::npos)
            << result.id << " nije u izvestaju";
    }
    EXPECT_NE(report.find("\"device\": \"03F0-2805\""), std::string::npos);
    EXPECT_NE(report.find("\"timestamp\": \"2026-08-21T10:00:00\""), std::string::npos);
}

// Izvestaj mora nositi PLAFON, ne samo efektivni nivo. Bez toga se ne moze
// znati da li je nesto preskoceno zato sto paket to ne sme ili zato sto niko
// nije trazio.
TEST_F(QualificationTest, ReportCarriesBothCeilingAndEffectiveLevel) {
    auto device = open(SafetyLevel::Lamp);
    ASSERT_NE(device, nullptr);

    const std::vector<CheckResult> results = runQualification(*device);
    const std::string report =
        formatReport(results, "03F0-2805", "2026-08-21T10:00:00", device->safety());

    EXPECT_NE(report.find("\"safetyCeiling\":"), std::string::npos);
    EXPECT_NE(report.find("\"effectiveLevel\": 2"), std::string::npos);
}

TEST_F(QualificationTest, ReportWordsAreStableAcrossOutcomes) {
    EXPECT_STREQ(toReportWord(CheckOutcome::Pass), "PASS");
    EXPECT_STREQ(toReportWord(CheckOutcome::Fail), "FAIL");
    EXPECT_STREQ(toReportWord(CheckOutcome::BlockedBySafetyLevel), "BLOCKED");
    EXPECT_STREQ(toReportWord(CheckOutcome::NotImplemented), "PENDING");
    EXPECT_STREQ(toReportWord(CheckOutcome::AsksTheUser), "ASK");
}

TEST_F(QualificationTest, SummaryAddsUp) {
    auto device = open(SafetyLevel::FullScan);
    ASSERT_NE(device, nullptr);

    const std::vector<CheckResult> results = runQualification(*device);
    const QualificationSummary summary = summarise(results);

    EXPECT_EQ(summary.total(), static_cast<int>(results.size()));
    EXPECT_EQ(summary.passed, countWith(results, CheckOutcome::Pass));
    EXPECT_EQ(summary.failed, countWith(results, CheckOutcome::Fail));
    EXPECT_EQ(summary.questions, countWith(results, CheckOutcome::AsksTheUser));
}

TEST(QualificationSummaryTest, EmptyRunIsCleanButEmpty) {
    const QualificationSummary summary = summarise({});
    EXPECT_EQ(summary.total(), 0);
    EXPECT_TRUE(summary.clean()) << "nula padova je nula padova";
}
