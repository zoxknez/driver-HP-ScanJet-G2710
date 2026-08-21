// Hardverska kvalifikacija - provere H1 do H13, kao izvrsivi koraci.
//
// Prijatelj ovo ne pokrece rukom; pokrece ga wizard, a wizard cita JSON koji
// odavde izlazi. Ista lista se moze pokrenuti i nad simulatorom, i to je
// acceptance gate faze G2710-11: cela isporuka se proba na OVOJ masini, bez
// uredjaja, pre nego sto ijedan fajl ode.
//
// Tri pravila koja ovaj modul sprovodi:
//
//   1. Provera koja trazi vise od efektivnog SafetyLevel-a se NE pokusava.
//      Ne prijavljuje se kao pad nego kao "nije pokrenuto, plafon je N".
//
//   2. Provera koja zahteva kod koji jos ne postoji prijavljuje TACAN razlog.
//      "H4 ceka port Head_Relocate" je upotrebljiv izvestaj; "fail" nije.
//
//   3. Ono sto masina ne moze da vidi pita se coveka. Da li lampa svetli ne
//      moze se procitati iz registra - moze se samo pogledati.
//
// Izlazni JSON je nadskup onoga sto tools/generate-status.py cita, pa se
// izvestaj sa prijateljevog racunara moze prekopirati u qualification/ i
// STATUS.md odmah pokazuje treci stubac.

#pragma once

#include "device/G2710Device.h"
#include "device/SafetyLevel.h"

#include <string>
#include <vector>

namespace g2710::cli {

enum class CheckOutcome {
    Pass,
    Fail,

    // Nije pokrenuto jer plafon build-a ne dozvoljava.
    BlockedBySafetyLevel,

    // Nije pokrenuto jer kod za to jos ne postoji. Razlog je uvek naveden.
    NotImplemented,

    // Masina ne moze da odgovori; pita se covek.
    AsksTheUser,
};

const char* toString(CheckOutcome outcome) noexcept;

// Kako se ishod upisuje u test-results.json.
const char* toReportWord(CheckOutcome outcome) noexcept;

struct CheckResult {
    std::string id;       // "H2.1"
    std::string name;
    SafetyLevel required = SafetyLevel::ReadOnly;
    CheckOutcome outcome = CheckOutcome::NotImplemented;

    // Sta je izmereno, ili zasto nije. Ide u izvestaj doslovno.
    std::string detail;

    // Za AsksTheUser: pitanje koje wizard postavlja, na koje se odgovara sa
    // DA ili NE.
    std::string question;
};

// Sazetak, da wizard ne mora da broji sam.
struct QualificationSummary {
    int passed = 0;
    int failed = 0;
    int blocked = 0;
    int notImplemented = 0;
    int questions = 0;

    int total() const noexcept {
        return passed + failed + blocked + notImplemented + questions;
    }

    // Da li je paket uopste stigao do kraja bez pada.
    bool clean() const noexcept { return failed == 0; }
};

QualificationSummary summarise(const std::vector<CheckResult>& results) noexcept;

// Pokreni sve provere koje efektivni nivo dozvoljava.
//
// Uredjaj mora biti otvoren i identifikovan. Sesija se NE zauzima ovde -
// pozivalac je vec zauzeo kroz begin().
std::vector<CheckResult> runQualification(G2710Device& device);

// Izvestaj u JSON. `deviceId` i `timestamp` dolaze spolja jer ih modul ne sme
// izmisljati - testovi bi tada bili nedeterministicki.
std::string formatReport(const std::vector<CheckResult>& results,
                         const std::string& deviceId, const std::string& timestamp,
                         const SafetyGate& gate);

}  // namespace g2710::cli
