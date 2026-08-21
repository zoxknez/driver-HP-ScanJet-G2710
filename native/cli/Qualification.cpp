#include "Qualification.h"

#include "G2710Profile.generated.h"
#include "rts8822/Lamp.h"
#include "rts8822/RegisterFile.h"
#include "rts8822/Registers.h"
#include "rts8822/ScanRegisters.h"
#include "scan/Capabilities.h"
#include "scan/ScanPlanner.h"
#include "scan/ScanSession.h"

#include <algorithm>
#include <cstdio>
#include <sstream>

namespace g2710::cli {
namespace {

// Sirina i visina probnog skena. Dovoljno da se vidi da podaci teku, dovoljno
// malo da prijatelj ne ceka.
constexpr int kProbeWidth = 128;
constexpr int kProbeLines = 24;

CheckResult passed(std::string id, std::string name, SafetyLevel level, std::string detail) {
    return {std::move(id), std::move(name), level, CheckOutcome::Pass, std::move(detail), {}};
}

CheckResult failed(std::string id, std::string name, SafetyLevel level, std::string detail) {
    return {std::move(id), std::move(name), level, CheckOutcome::Fail, std::move(detail), {}};
}

CheckResult blocked(std::string id, std::string name, SafetyLevel level,
                    const SafetyGate& gate) {
    std::ostringstream detail;
    detail << "trazi nivo " << toInt(level) << ", efektivni je " << toInt(gate.effective());
    if (gate.effective() != gate.requested()) {
        detail << " (plafon build-a je " << toInt(gate.ceiling()) << ")";
    }
    return {std::move(id), std::move(name), level, CheckOutcome::BlockedBySafetyLevel,
            detail.str(), {}};
}

CheckResult pending(std::string id, std::string name, SafetyLevel level, std::string why) {
    return {std::move(id), std::move(name), level, CheckOutcome::NotImplemented,
            std::move(why), {}};
}

CheckResult question(std::string id, std::string name, SafetyLevel level, std::string prompt) {
    return {std::move(id), std::move(name), level, CheckOutcome::AsksTheUser, {},
            std::move(prompt)};
}

std::string describe(const Error& error) {
    std::ostringstream text;
    text << toString(error.code);
    if (error.context != nullptr && error.context[0] != '\0') {
        text << " (" << error.context << ")";
    }
    if (error.win32 != 0) {
        text << " win32=" << error.win32;
    }
    return text.str();
}

// --- H1 i H2: enumeracija i citanje ------------------------------------------

void checkTransport(G2710Device& device, std::vector<CheckResult>* out) {
    const DeviceIdentity& identity = device.identity();

    std::ostringstream detail;
    std::snprintf(nullptr, 0, "%s", "");  // drzi <cstdio> u upotrebi
    char buffer[64] = {};
    std::snprintf(buffer, sizeof(buffer), "%04X:%04X", identity.vendorId, identity.productId);
    detail << buffer;

    if (identity.vendorId == profile::kUsbVendorId &&
        identity.productId == profile::kUsbProductId) {
        out->push_back(passed("H1.1", "USB identitet", SafetyLevel::ReadOnly, detail.str()));
    } else {
        detail << ", ocekivano " << profile::kUsbVendorId << ":" << profile::kUsbProductId;
        out->push_back(failed("H1.1", "USB identitet", SafetyLevel::ReadOnly, detail.str()));
    }

    auto pipes = device.transport().pipeConfiguration();
    if (!pipes) {
        out->push_back(failed("H1.2", "Konfiguracija pipe-ova", SafetyLevel::ReadOnly,
                              describe(pipes.error())));
        return;
    }

    std::ostringstream pipeText;
    pipeText << "bulk IN 0x" << std::hex << static_cast<int>(pipes.value().bulkIn)
             << ", bulk OUT 0x" << static_cast<int>(pipes.value().bulkOut);
    if (pipes.value().hasInterrupt) {
        pipeText << ", interrupt 0x" << static_cast<int>(pipes.value().interrupt);
    } else {
        pipeText << ", BEZ interrupt pipe-a";
    }

    // Bez interrupt pipe-a dugmad ne mogu da rade. To nije pad enumeracije,
    // ali jeste nalaz koji H10 mora da zna unapred.
    if (pipes.value().bulkIn == 0 || pipes.value().bulkOut == 0) {
        out->push_back(failed("H1.2", "Konfiguracija pipe-ova", SafetyLevel::ReadOnly,
                              pipeText.str()));
    } else {
        out->push_back(
            passed("H1.2", "Konfiguracija pipe-ova", SafetyLevel::ReadOnly, pipeText.str()));
    }
}

void checkRegisters(G2710Device& device, std::vector<CheckResult>* out) {
    rts8822::RegisterFile registers{device.transport()};

    // Citanje celog bank-a je najjaci dokaz da vendor control transferi rade.
    std::vector<std::byte> bank(profile::kRegisterBankLength);
    if (const Status read = registers.readBank(bank); !read) {
        out->push_back(failed("H2.1", "Citanje registarskog bank-a", SafetyLevel::ReadOnly,
                              describe(read.error())));
        return;
    }

    // Bank pun nula znaci da transfer "uspeva" a ne cita nista - otkaz koji
    // izgleda kao uspeh.
    const bool allZero = std::all_of(bank.begin(), bank.end(),
                                     [](std::byte value) { return value == std::byte{0}; });
    std::ostringstream detail;
    detail << profile::kRegisterBankLength << " bajtova sa 0x" << std::hex
           << profile::kRegisterBankBase;

    if (allZero) {
        detail << " - SVE NULE, transfer prolazi ali ne cita";
        out->push_back(
            failed("H2.1", "Citanje registarskog bank-a", SafetyLevel::ReadOnly, detail.str()));
    } else {
        out->push_back(
            passed("H2.1", "Citanje registarskog bank-a", SafetyLevel::ReadOnly, detail.str()));
    }

    const auto home = device.isHeadAtHome();
    if (!home) {
        out->push_back(failed("H2.2", "Home senzor", SafetyLevel::ReadOnly,
                              describe(home.error())));
    } else {
        out->push_back(passed("H2.2", "Home senzor", SafetyLevel::ReadOnly,
                              home.value() ? "glava je na home poziciji"
                                           : "glava NIJE na home poziciji"));
    }
}

// --- H3: lampa ---------------------------------------------------------------

void checkLamp(G2710Device& device, std::vector<CheckResult>* out) {
    const SafetyGate& gate = device.safety();

    if (toInt(gate.effective()) < toInt(SafetyLevel::Lamp)) {
        out->push_back(blocked("H3.1", "Paljenje lampe", SafetyLevel::Lamp, gate));
        out->push_back(blocked("H3.2", "Status lampe", SafetyLevel::Lamp, gate));
        out->push_back(blocked("H3.3", "Lampa vidljivo svetli", SafetyLevel::Lamp, gate));
        return;
    }

    rts8822::RegisterFile registers{device.transport()};
    rts8822::Lamp lamp{registers, gate};

    if (const Status lit = lamp.setLamp(rts8822::LampKind::Flatbed, true); !lit) {
        out->push_back(
            failed("H3.1", "Paljenje lampe", SafetyLevel::Lamp, describe(lit.error())));
        out->push_back(blocked("H3.2", "Status lampe", SafetyLevel::Lamp, gate));
        return;
    }
    if (const Status pwm = lamp.setupPwm(rts8822::LampKind::Flatbed); !pwm) {
        out->push_back(failed("H3.1", "Paljenje lampe", SafetyLevel::Lamp,
                              "PWM: " + describe(pwm.error())));
        return;
    }
    out->push_back(passed("H3.1", "Paljenje lampe", SafetyLevel::Lamp, "komanda prihvacena"));

    const auto status = device.lampStatus();
    if (!status) {
        out->push_back(
            failed("H3.2", "Status lampe", SafetyLevel::Lamp, describe(status.error())));
    } else if (!status.value().flatbedOn) {
        out->push_back(failed("H3.2", "Status lampe", SafetyLevel::Lamp,
                              "upisano ukljuceno, procitano iskljuceno"));
    } else {
        out->push_back(
            passed("H3.2", "Status lampe", SafetyLevel::Lamp, "cita se kao ukljucena"));
    }

    // Registar moze reci "ukljucena" a lampa ne svetli. Jedini nacin da se to
    // razdvoji je pogledati.
    out->push_back(question("H3.3", "Lampa vidljivo svetli", SafetyLevel::Lamp,
                            "Da li lampa skenera SADA svetli?"));
}

// --- H4: kretanje ------------------------------------------------------------

void checkMotion(G2710Device& device, std::vector<CheckResult>* out) {
    const SafetyGate& gate = device.safety();

    if (toInt(gate.effective()) < toInt(SafetyLevel::Motor)) {
        out->push_back(blocked("H4.1", "Povratak na home", SafetyLevel::Motor, gate));
        return;
    }

    // Kretanje glave trazi port Head_Relocate i Head_ParkHome, koji jos ne
    // postoji. Izmisljati ga znacilo bi pomerati tudji motor po pretpostavci -
    // upravo ono sto ceo projekat izbegava.
    out->push_back(pending("H4.1", "Povratak na home", SafetyLevel::Motor,
                           "ceka port Head_Relocate; do tada se glava ne pomera"));
}

// --- H5 i H6: akvizicija -----------------------------------------------------

void checkAcquisition(G2710Device& device, std::vector<CheckResult>* out) {
    const SafetyGate& gate = device.safety();

    if (toInt(gate.effective()) < toInt(SafetyLevel::FullScan)) {
        out->push_back(blocked("H5.1", "Probni prolaz, sivo", SafetyLevel::FullScan, gate));
        out->push_back(blocked("H6.1", "Probni prolaz, boja", SafetyLevel::FullScan, gate));
        return;
    }

    struct Probe {
        const char* id;
        const char* name;
        image::ColorMode mode;
    };
    const Probe probes[] = {
        {"H5.1", "Probni prolaz, sivo", image::ColorMode::Gray},
        {"H6.1", "Probni prolaz, boja", image::ColorMode::Color},
    };

    rts8822::RegisterFile registers{device.transport()};

    for (const Probe& probe : probes) {
        scan::ScanRequest request;
        request.resolution = 300;
        request.colorMode = probe.mode;
        request.depth = 8;
        request.region = {0, 0, kProbeWidth, kProbeLines};

        // Kvalifikacija SME nepotvrdjene rezolucije - ona ih upravo potvrdjuje.
        request.allowUnqualified = true;

        auto planned = scan::planScan(request);
        if (!planned) {
            out->push_back(failed(probe.id, probe.name, SafetyLevel::FullScan,
                                  "plan: " + describe(planned.error())));
            continue;
        }

        scan::ScanSession session{registers, gate, planned.value()};
        if (const Status begun = session.begin(); !begun) {
            out->push_back(failed(probe.id, probe.name, SafetyLevel::FullScan,
                                  describe(begun.error())));
            continue;
        }

        std::vector<std::uint8_t> line(session.outputBytesPerLine(), 0);
        int delivered = 0;
        long long sum = 0;
        Error lastError{};
        bool broke = false;

        for (;;) {
            auto more = session.nextLine(line, device.cancellation());
            if (!more) {
                lastError = more.error();
                broke = true;
                break;
            }
            if (!more.value()) {
                break;
            }
            ++delivered;
            for (std::uint8_t value : line) {
                sum += value;
            }
        }

        if (broke) {
            out->push_back(
                failed(probe.id, probe.name, SafetyLevel::FullScan, describe(lastError)));
            continue;
        }

        std::ostringstream detail;
        detail << delivered << " od " << session.expectedOutputLines() << " redova";
        if (delivered > 0 && !line.empty()) {
            detail << ", prosek " << (sum / (static_cast<long long>(delivered) *
                                             static_cast<long long>(line.size())));
        }

        if (delivered != session.expectedOutputLines()) {
            out->push_back(failed(probe.id, probe.name, SafetyLevel::FullScan, detail.str()));
        } else if (sum == 0) {
            // Redovi stizu, ali su svi crni. Lampa ili senzor.
            detail << " - SVE NULE";
            out->push_back(failed(probe.id, probe.name, SafetyLevel::FullScan, detail.str()));
        } else {
            out->push_back(passed(probe.id, probe.name, SafetyLevel::FullScan, detail.str()));
        }
    }
}

// --- H8: rezolucije ----------------------------------------------------------

void checkResolutions(G2710Device& device, std::vector<CheckResult>* out) {
    const SafetyGate& gate = device.safety();

    for (int dpi : scan::executableResolutions()) {
        char id[16] = {};
        std::snprintf(id, sizeof(id), "H8.%d", dpi);

        char name[48] = {};
        std::snprintf(name, sizeof(name), "%d dpi, boja", dpi);

        if (toInt(gate.effective()) < toInt(SafetyLevel::FullScan)) {
            out->push_back(blocked(id, name, SafetyLevel::FullScan, gate));
            continue;
        }

        scan::ScanRequest request;
        request.resolution = dpi;
        request.colorMode = image::ColorMode::Color;
        request.depth = 8;
        request.region = {0, 0, kProbeWidth, 8};
        request.allowUnqualified = true;

        auto planned = scan::planScan(request);
        if (!planned) {
            out->push_back(
                failed(id, name, SafetyLevel::FullScan, "plan: " + describe(planned.error())));
            continue;
        }

        rts8822::RegisterFile registers{device.transport()};
        scan::ScanSession session{registers, gate, planned.value()};
        if (const Status begun = session.begin(); !begun) {
            out->push_back(failed(id, name, SafetyLevel::FullScan, describe(begun.error())));
            continue;
        }

        std::vector<std::uint8_t> line(session.outputBytesPerLine(), 0);
        int delivered = 0;
        bool broke = false;
        Error lastError{};

        for (;;) {
            auto more = session.nextLine(line, device.cancellation());
            if (!more) {
                lastError = more.error();
                broke = true;
                break;
            }
            if (!more.value()) {
                break;
            }
            ++delivered;
        }

        std::ostringstream detail;
        detail << "skenira na " << planned.value().nativeResolution << " dpi, " << delivered
               << " redova";
        if (!planned.value().useHardwareAlignment &&
            planned.value().tableMode == image::ColorMode::Color) {
            detail << ", softversko poravnanje (D3)";
        }

        if (broke) {
            out->push_back(failed(id, name, SafetyLevel::FullScan, describe(lastError)));
        } else if (delivered != session.expectedOutputLines()) {
            out->push_back(failed(id, name, SafetyLevel::FullScan, detail.str()));
        } else {
            out->push_back(passed(id, name, SafetyLevel::FullScan, detail.str()));
        }
    }
}

// --- H10 do H13 --------------------------------------------------------------

void checkRemaining(G2710Device& device, std::vector<CheckResult>* out) {
    const SafetyGate& gate = device.safety();

    out->push_back(question("H10.1", "Dugme Scan", SafetyLevel::ReadOnly,
                            "Pritisnite dugme Scan na skeneru. Da li se nesto desilo?"));

    out->push_back(pending("H11.1", "WIA integracija", SafetyLevel::FullScan,
                           "meri se iz Windows Scan aplikacije, ne iz ovog alata"));
    out->push_back(pending("H12.1", "TWAIN x64 i x86", SafetyLevel::FullScan,
                           "TWAIN Data Source jos nije izgradjen"));

    if (toInt(gate.effective()) < toInt(SafetyLevel::FullScan)) {
        out->push_back(blocked("H13.1", "Stres: ponovljeni prolazi", SafetyLevel::FullScan,
                               gate));
        return;
    }
    out->push_back(pending("H13.1", "Stres: ponovljeni prolazi", SafetyLevel::FullScan,
                           "pokrece se posebno, posle H6 - ne u prvom prolazu"));
}

}  // namespace

const char* toString(CheckOutcome outcome) noexcept {
    switch (outcome) {
        case CheckOutcome::Pass:                 return "prosao";
        case CheckOutcome::Fail:                 return "PAO";
        case CheckOutcome::BlockedBySafetyLevel: return "nije pokrenut (plafon)";
        case CheckOutcome::NotImplemented:       return "nije pokrenut (nema koda)";
        case CheckOutcome::AsksTheUser:          return "pitanje za korisnika";
    }
    return "?";
}

const char* toReportWord(CheckOutcome outcome) noexcept {
    switch (outcome) {
        case CheckOutcome::Pass:                 return "PASS";
        case CheckOutcome::Fail:                 return "FAIL";
        case CheckOutcome::BlockedBySafetyLevel: return "BLOCKED";
        case CheckOutcome::NotImplemented:       return "PENDING";
        case CheckOutcome::AsksTheUser:          return "ASK";
    }
    return "?";
}

QualificationSummary summarise(const std::vector<CheckResult>& results) noexcept {
    QualificationSummary summary;
    for (const CheckResult& result : results) {
        switch (result.outcome) {
            case CheckOutcome::Pass:                 ++summary.passed; break;
            case CheckOutcome::Fail:                 ++summary.failed; break;
            case CheckOutcome::BlockedBySafetyLevel: ++summary.blocked; break;
            case CheckOutcome::NotImplemented:       ++summary.notImplemented; break;
            case CheckOutcome::AsksTheUser:          ++summary.questions; break;
        }
    }
    return summary;
}

std::vector<CheckResult> runQualification(G2710Device& device) {
    std::vector<CheckResult> results;

    // Redosled je redosled RIZIKA: prvo ono sto nista ne pomera.
    checkTransport(device, &results);
    checkRegisters(device, &results);
    checkLamp(device, &results);
    checkMotion(device, &results);
    checkAcquisition(device, &results);
    checkResolutions(device, &results);
    checkRemaining(device, &results);

    return results;
}

std::string formatReport(const std::vector<CheckResult>& results, const std::string& deviceId,
                         const std::string& timestamp, const SafetyGate& gate) {
    const QualificationSummary summary = summarise(results);

    std::ostringstream json;
    json << "{\n";
    json << "  \"device\": \"" << deviceId << "\",\n";
    json << "  \"timestamp\": \"" << timestamp << "\",\n";
    json << "  \"safetyCeiling\": " << toInt(gate.ceiling()) << ",\n";
    json << "  \"effectiveLevel\": " << toInt(gate.effective()) << ",\n";
    json << "  \"summary\": {\"pass\": " << summary.passed << ", \"fail\": " << summary.failed
         << ", \"blocked\": " << summary.blocked
         << ", \"pending\": " << summary.notImplemented
         << ", \"ask\": " << summary.questions << "},\n";
    json << "  \"tests\": [\n";

    for (std::size_t i = 0; i < results.size(); ++i) {
        const CheckResult& result = results[i];
        json << "    {\"id\": \"" << result.id << "\", \"name\": \"" << result.name
             << "\", \"result\": \"" << toReportWord(result.outcome) << "\", \"level\": "
             << toInt(result.required) << ", \"date\": \"" << timestamp << "\"";
        if (!result.detail.empty()) {
            json << ", \"detail\": \"" << result.detail << "\"";
        }
        if (!result.question.empty()) {
            json << ", \"question\": \"" << result.question << "\"";
        }
        json << "}" << (i + 1 == results.size() ? "" : ",") << "\n";
    }

    json << "  ]\n}\n";
    return json.str();
}

}  // namespace g2710::cli
