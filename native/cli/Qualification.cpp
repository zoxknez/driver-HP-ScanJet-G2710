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
    detail << "asks for level " << toInt(level) << ", effective is " << toInt(gate.effective());
    if (gate.effective() != gate.requested()) {
        detail << " (the build ceiling is " << toInt(gate.ceiling()) << ")";
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
        out->push_back(passed("H1.1", "USB identity", SafetyLevel::ReadOnly, detail.str()));
    } else {
        detail << ", expected " << profile::kUsbVendorId << ":" << profile::kUsbProductId;
        out->push_back(failed("H1.1", "USB identity", SafetyLevel::ReadOnly, detail.str()));
    }

    auto pipes = device.transport().pipeConfiguration();
    if (!pipes) {
        out->push_back(failed("H1.2", "Pipe configuration", SafetyLevel::ReadOnly,
                              describe(pipes.error())));
        return;
    }

    std::ostringstream pipeText;
    pipeText << "bulk IN 0x" << std::hex << static_cast<int>(pipes.value().bulkIn)
             << ", bulk OUT 0x" << static_cast<int>(pipes.value().bulkOut);
    if (pipes.value().hasInterrupt) {
        pipeText << ", interrupt 0x" << static_cast<int>(pipes.value().interrupt);
    } else {
        pipeText << ", NO interrupt pipe";
    }

    // Bez interrupt pipe-a dugmad ne mogu da rade. To nije pad enumeracije,
    // ali jeste nalaz koji H10 mora da zna unapred.
    if (pipes.value().bulkIn == 0 || pipes.value().bulkOut == 0) {
        out->push_back(failed("H1.2", "Pipe configuration", SafetyLevel::ReadOnly,
                              pipeText.str()));
    } else {
        out->push_back(
            passed("H1.2", "Pipe configuration", SafetyLevel::ReadOnly, pipeText.str()));
    }
}

void checkRegisters(G2710Device& device, std::vector<CheckResult>* out) {
    rts8822::RegisterFile registers{device.transport()};

    // Citanje celog bank-a je najjaci dokaz da vendor control transferi rade.
    std::vector<std::byte> bank(profile::kRegisterBankLength);
    if (const Status read = registers.readBank(bank); !read) {
        out->push_back(failed("H2.1", "Register bank read", SafetyLevel::ReadOnly,
                              describe(read.error())));
        return;
    }

    // Bank pun nula znaci da transfer "uspeva" a ne cita nista - otkaz koji
    // izgleda kao uspeh.
    const bool allZero = std::all_of(bank.begin(), bank.end(),
                                     [](std::byte value) { return value == std::byte{0}; });
    std::ostringstream detail;
    detail << profile::kRegisterBankLength << " bytes from 0x" << std::hex
           << profile::kRegisterBankBase;

    if (allZero) {
        detail << " - ALL ZEROES: the transfer succeeds but reads nothing";
        out->push_back(
            failed("H2.1", "Register bank read", SafetyLevel::ReadOnly, detail.str()));
    } else {
        out->push_back(
            passed("H2.1", "Register bank read", SafetyLevel::ReadOnly, detail.str()));
    }

    const auto home = device.isHeadAtHome();
    if (!home) {
        out->push_back(failed("H2.2", "Home sensor", SafetyLevel::ReadOnly,
                              describe(home.error())));
    } else {
        out->push_back(passed("H2.2", "Home sensor", SafetyLevel::ReadOnly,
                              home.value() ? "the head is at the home position"
                                           : "the head is NOT at the home position"));
    }
}

// --- H3: lampa ---------------------------------------------------------------

void checkLamp(G2710Device& device, std::vector<CheckResult>* out) {
    const SafetyGate& gate = device.safety();

    if (toInt(gate.effective()) < toInt(SafetyLevel::Lamp)) {
        out->push_back(blocked("H3.1", "Lamp switch-on", SafetyLevel::Lamp, gate));
        out->push_back(blocked("H3.2", "Lamp status", SafetyLevel::Lamp, gate));
        out->push_back(blocked("H3.3", "The lamp is visibly lit", SafetyLevel::Lamp, gate));
        return;
    }

    rts8822::RegisterFile registers{device.transport()};
    rts8822::Lamp lamp{registers, gate};

    if (const Status lit = lamp.setLamp(rts8822::LampKind::Flatbed, true); !lit) {
        out->push_back(
            failed("H3.1", "Lamp switch-on", SafetyLevel::Lamp, describe(lit.error())));
        out->push_back(blocked("H3.2", "Lamp status", SafetyLevel::Lamp, gate));
        return;
    }
    if (const Status pwm = lamp.setupPwm(rts8822::LampKind::Flatbed); !pwm) {
        out->push_back(failed("H3.1", "Lamp switch-on", SafetyLevel::Lamp,
                              "PWM: " + describe(pwm.error())));
        return;
    }
    out->push_back(passed("H3.1", "Lamp switch-on", SafetyLevel::Lamp, "command accepted"));

    const auto status = device.lampStatus();
    if (!status) {
        out->push_back(
            failed("H3.2", "Lamp status", SafetyLevel::Lamp, describe(status.error())));
    } else if (!status.value().flatbedOn) {
        out->push_back(failed("H3.2", "Lamp status", SafetyLevel::Lamp,
                              "written on, read back off"));
    } else {
        out->push_back(
            passed("H3.2", "Lamp status", SafetyLevel::Lamp, "reads back as on"));
    }

    // Registar moze reci "ukljucena" a lampa ne svetli. Jedini nacin da se to
    // razdvoji je pogledati.
    out->push_back(question("H3.3", "The lamp is visibly lit", SafetyLevel::Lamp,
                            "Is the scanner lamp lit RIGHT NOW?"));
}

// --- H4: kretanje ------------------------------------------------------------

void checkMotion(G2710Device& device, std::vector<CheckResult>* out) {
    const SafetyGate& gate = device.safety();

    if (toInt(gate.effective()) < toInt(SafetyLevel::Motor)) {
        out->push_back(blocked("H4.1", "Return to home", SafetyLevel::Motor, gate));
        return;
    }

    // Kretanje glave trazi port Head_Relocate i Head_ParkHome, koji jos ne
    // postoji. Izmisljati ga znacilo bi pomerati tudji motor po pretpostavci -
    // upravo ono sto ceo projekat izbegava.
    out->push_back(pending("H4.1", "Return to home", SafetyLevel::Motor,
                           "waiting on the Head_Relocate port; until then the head does not move"));
}

// --- H5 i H6: akvizicija -----------------------------------------------------

void checkAcquisition(G2710Device& device, std::vector<CheckResult>* out) {
    const SafetyGate& gate = device.safety();

    if (toInt(gate.effective()) < toInt(SafetyLevel::FullScan)) {
        out->push_back(blocked("H5.1", "Test pass, greyscale", SafetyLevel::FullScan, gate));
        out->push_back(blocked("H6.1", "Test pass, colour", SafetyLevel::FullScan, gate));
        return;
    }

    struct Probe {
        const char* id;
        const char* name;
        image::ColorMode mode;
    };
    const Probe probes[] = {
        {"H5.1", "Test pass, greyscale", image::ColorMode::Gray},
        {"H6.1", "Test pass, colour", image::ColorMode::Color},
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
        detail << delivered << " of " << session.expectedOutputLines() << " lines";
        if (delivered > 0 && !line.empty()) {
            detail << ", average " << (sum / (static_cast<long long>(delivered) *
                                             static_cast<long long>(line.size())));
        }

        if (delivered != session.expectedOutputLines()) {
            out->push_back(failed(probe.id, probe.name, SafetyLevel::FullScan, detail.str()));
        } else if (sum == 0) {
            // Redovi stizu, ali su svi crni. Lampa ili senzor.
            detail << " - ALL ZEROES";
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
        std::snprintf(name, sizeof(name), "%d dpi, colour", dpi);

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
        detail << "scans at " << planned.value().nativeResolution << " dpi, " << delivered
               << " lines";
        if (!planned.value().useHardwareAlignment &&
            planned.value().tableMode == image::ColorMode::Color) {
            detail << ", software alignment (D3)";
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

    out->push_back(question("H10.1", "Scan button", SafetyLevel::ReadOnly,
                            "Press the Scan button on the scanner. Did anything happen?"));

    out->push_back(pending("H11.1", "WIA integration", SafetyLevel::FullScan,
                           "measured from the Windows Scan application, not from this tool"));
    out->push_back(pending("H12.1", "TWAIN x64 and x86", SafetyLevel::FullScan,
                           "The TWAIN Data Source has not been built yet"));

    if (toInt(gate.effective()) < toInt(SafetyLevel::FullScan)) {
        out->push_back(blocked("H13.1", "Stress: repeated passes", SafetyLevel::FullScan,
                               gate));
        return;
    }
    out->push_back(pending("H13.1", "Stress: repeated passes", SafetyLevel::FullScan,
                           "run separately, after H6 - not in the first pass"));
}

}  // namespace

const char* toString(CheckOutcome outcome) noexcept {
    switch (outcome) {
        case CheckOutcome::Pass:                 return "passed";
        case CheckOutcome::Fail:                 return "FAILED";
        case CheckOutcome::BlockedBySafetyLevel: return "not run (ceiling)";
        case CheckOutcome::NotImplemented:       return "not run (no code)";
        case CheckOutcome::AsksTheUser:          return "a question for the user";
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
                         const std::string& timestamp, const SafetyGate& gate,
                         const std::string& transport) {
    const QualificationSummary summary = summarise(results);

    std::ostringstream json;
    json << "{\n";
    json << "  \"device\": \"" << deviceId << "\",\n";
    // Odmah posle uredjaja, da se vidi i kada neko samo baci pogled na fajl.
    json << "  \"transport\": \"" << transport << "\",\n";
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
