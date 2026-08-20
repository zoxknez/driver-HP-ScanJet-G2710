// g2710ctl - dijagnosticki CLI.
//
// Sve ide kroz iste slojeve koje koriste WIA, TWAIN i aplikacija: isti
// TransportProvider, isti DeviceArbiter, isti SafetyGate. Ono sto ovde radi,
// radi i tamo - i obrnuto.

#include "device/G2710Device.h"
#include "device/SafetyLevel.h"
#include "rts8822/Rts8822.h"
#include "scan/Capabilities.h"
#include "scan/ScanPlanner.h"
#include "transport/ITransportProvider.h"
#include "transport/UsbScanTransport.h"
#include "G2710Profile.generated.h"

#include "../sim/SimTransport.h"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

using namespace g2710;

namespace {

constexpr const char* kUsage =
    "g2710ctl - HP ScanJet G2710 dijagnostika\n"
    "\n"
    "  g2710ctl probe    [opcije]   identitet uredjaja i konfiguracija pipe-ova\n"
    "  g2710ctl regdump  [opcije]   ispis registarskog bank-a\n"
    "  g2710ctl status   [opcije]   senzori, lampe, PWM (sve read-only)\n"
    "  g2710ctl info                ugradjeni profil i granice build-a\n"
    "\n"
    "Opcije:\n"
    "  --transport <usbscan|sim>    podrazumevano usbscan\n"
    "  --safety-level <1..5>        zahtevani nivo; efektivni je\n"
    "                               min(BuildSafetyCeiling, zahtevani)\n"
    "  --device <putanja>           podrazumevano \\\\.\\Usbscan0\n";

struct Options {
    std::string command;
    std::string transport = "usbscan";
    std::wstring devicePath;
    SafetyLevel requested = SafetyLevel::ReadOnly;
    bool json = false;
};

bool parse(int argc, char** argv, Options* out) {
    if (argc < 2) {
        return false;
    }
    out->command = argv[1];

    for (int i = 2; i < argc; ++i) {
        const std::string arg = argv[i];
        const bool hasNext = (i + 1) < argc;

        if (arg == "--json") {
            out->json = true;
        } else if (arg == "--transport" && hasNext) {
            out->transport = argv[++i];
        } else if (arg == "--device" && hasNext) {
            const std::string value = argv[++i];
            out->devicePath.assign(value.begin(), value.end());
        } else if (arg == "--safety-level" && hasNext) {
            const int level = std::atoi(argv[++i]);
            if (level < 1 || level > 5) {
                std::fprintf(stderr, "safety-level mora biti 1..5\n");
                return false;
            }
            out->requested = static_cast<SafetyLevel>(level);
        } else {
            std::fprintf(stderr, "nepoznat argument: %s\n", arg.c_str());
            return false;
        }
    }
    return true;
}

void reportError(const char* what, const Error& error) {
    std::fprintf(stderr, "%s: %s", what, toString(error.code));
    if (error.context != nullptr && error.context[0] != '\0') {
        std::fprintf(stderr, " (%s)", error.context);
    }
    if (error.win32 != 0) {
        std::fprintf(stderr, " win32=%lu", static_cast<unsigned long>(error.win32));
    }
    std::fputc('\n', stderr);
}

int cmdInfo(const SafetyGate& gate) {
    std::printf("G2710 ugradjeni profil\n");
    std::printf("  USB                 %04X:%04X\n",
                profile::kUsbVendorId, profile::kUsbProductId);
    std::printf("  Chipset             %s\n", profile::kChipsetName);
    std::printf("  Senzor              %d dpi, line_dist=%d, evenodd_dist=%d\n",
                profile::kSensor.resolution, profile::kSensor.lineDistance,
                profile::kSensor.evenOddDistance);
    std::printf("  Motor               %d dpi\n", profile::kMotor.resolution);
    std::printf("  Register bank       0x%04X, %d bajtova\n",
                profile::kRegisterBankBase, profile::kRegisterBankLength);
    std::printf("  Dugmad              %d\n", profile::kButtons.count);
    std::printf("\n");
    std::printf("Bezbednost\n");
    std::printf("  BuildSafetyCeiling  %s\n", toString(gate.ceiling()));
    std::printf("  Zahtevano           %s\n", toString(gate.requested()));
    std::printf("  Efektivno           %s%s\n", toString(gate.effective()),
                gate.wasClamped() ? "   (spusteno na plafon build-a)" : "");
    std::printf("  Motor path          %s\n",
                G2710_MOTOR_PATH_COMPILED ? "kompajliran" : "NIJE kompajliran");
    return 0;
}

// Tabela mogucnosti. Ne dodiruje uredjaj - sve je staticki racun, pa radi i
// kada skenera nema. Odatle se generise docs/STATUS.md.
int cmdCapabilities(bool asJson) {
    using namespace g2710::scan;

    struct Row {
        const ResolutionCapability* capability;
        bool planned;
        ScanPlan plan;
    };

    std::vector<Row> rows;
    for (const auto& capability : flatbedResolutions()) {
        ScanRequest request;
        request.resolution = capability.dpi;
        request.colorMode = image::ColorMode::Color;
        request.allowUnqualified = true;  // dijagnostika sme; WIA i TWAIN ne

        Row row{&capability, false, {}};
        if (auto plan = planScan(request)) {
            row.planned = true;
            row.plan = plan.value();
        }
        rows.push_back(row);
    }

    const std::vector<int> advertisable = advertisableResolutions();

    if (!asJson) {
        std::printf("Flatbed rezolucije\n");
        std::printf("  %6s  %-7s  %-19s  %-6s  %-9s  %s\n",
                    "dpi", "izvor", "status", "native", "poravnanje", "napomena");
        for (const auto& row : rows) {
            std::printf("  %6d  %-7s  %-19s  %-6d  %-9s  %s\n",
                        row.capability->dpi, toString(row.capability->origin),
                        toString(row.capability->level),
                        row.planned ? row.plan.nativeResolution : 0,
                        row.planned ? (row.plan.useHardwareAlignment ? "hardver" : "softver")
                                    : "-",
                        row.capability->note);
        }
        std::printf("\nDubine\n");
        for (const auto& depth : depthCapabilities()) {
            std::printf("  %6d  %-19s  %s\n", depth.bits, toString(depth.level), depth.note);
        }
        std::printf("\nOglasava se kroz WIA i TWAIN: ");
        if (advertisable.empty()) {
            std::printf("nista - nijedna vrednost jos nije hardverski potvrdjena\n");
        } else {
            for (std::size_t i = 0; i < advertisable.size(); ++i) {
                std::printf("%s%d", i == 0 ? "" : ", ", advertisable[i]);
            }
            std::printf("\n");
        }
        return 0;
    }

    std::printf("{\n");
    std::printf("  \"device\": \"%04X:%04X\",\n", profile::kUsbVendorId, profile::kUsbProductId);
    std::printf("  \"resolutions\": [\n");
    for (std::size_t i = 0; i < rows.size(); ++i) {
        const auto& row = rows[i];
        std::printf("    {\"dpi\": %d, \"origin\": \"%s\", \"level\": \"%s\", "
                    "\"sourceDpi\": %d, \"nativeDpi\": %d, \"resize\": \"%s\", "
                    "\"alignment\": \"%s\", \"advertisable\": %s, \"note\": \"%s\"}%s\n",
                    row.capability->dpi, toString(row.capability->origin),
                    toString(row.capability->level), row.capability->sourceDpi,
                    row.planned ? row.plan.nativeResolution : 0,
                    row.planned ? toString(row.plan.resize) : "-",
                    row.planned ? (row.plan.useHardwareAlignment ? "hardware" : "software") : "-",
                    row.capability->advertisable() ? "true" : "false",
                    row.capability->note,
                    i + 1 == rows.size() ? "" : ",");
    }
    std::printf("  ],\n");
    std::printf("  \"depths\": [\n");
    const auto depths = depthCapabilities();
    for (std::size_t i = 0; i < depths.size(); ++i) {
        std::printf("    {\"bits\": %d, \"level\": \"%s\", \"note\": \"%s\"}%s\n",
                    depths[i].bits, toString(depths[i].level), depths[i].note,
                    i + 1 == depths.size() ? "" : ",");
    }
    std::printf("  ],\n");
    std::printf("  \"advertisable\": [");
    for (std::size_t i = 0; i < advertisable.size(); ++i) {
        std::printf("%s%d", i == 0 ? "" : ", ", advertisable[i]);
    }
    std::printf("]\n}\n");
    return 0;
}

int cmdProbe(ITransport& transport, const SafetyGate& gate) {
    // Enumeracija i citanje descriptor-a su nivo 1 - nista se ne pomera.
    if (const Status allowed = gate.require(SafetyLevel::ReadOnly, "probe"); !allowed) {
        reportError("probe", allowed.error());
        return 2;
    }

    std::printf("Transport             %s\n", transport.name());
    std::printf("Provider              %s\n", TransportProvider::activeProviderName());

    auto pipes = transport.pipeConfiguration();
    if (!pipes) {
        reportError("pipeConfiguration", pipes.error());
        return 3;
    }
    std::printf("Bulk IN endpoint      0x%02X %s\n", pipes.value().bulkIn,
                pipes.value().bulkIn == profile::kBulkInEndpoint ? "(ocekivano)" : "(NEOCEKIVANO)");
    std::printf("Bulk OUT endpoint     0x%02X %s\n", pipes.value().bulkOut,
                pipes.value().bulkOut == profile::kBulkOutEndpoint ? "(ocekivano)" : "(NEOCEKIVANO)");
    std::printf("Interrupt pipe        %s\n",
                pipes.value().hasInterrupt ? "postoji (dugmad dostupna)" : "NEMA");

    // Identitet se cita sa uredjaja, ne pretpostavlja iz INF-a.
    if (auto* usbscan = dynamic_cast<UsbScanTransport*>(&transport); usbscan != nullptr) {
        auto id = usbscan->identity();
        if (!id) {
            reportError("identity", id.error());
            return 4;
        }
        const bool matches = id.value().vendorId == profile::kUsbVendorId &&
                             id.value().productId == profile::kUsbProductId;
        std::printf("USB id                %04X:%04X %s\n",
                    id.value().vendorId, id.value().productId,
                    matches ? "(HP ScanJet G2710)" : "(NE ODGOVARA G2710)");
        std::printf("bcdDevice             %04X\n", id.value().bcdDevice);
        if (!matches) {
            return 5;
        }
    }

    std::printf("Max control chunk     %zu %s\n", transport.maxControlChunk(),
                transport.maxControlChunk() == 0 ? "(bez deljenja)" : "");
    return 0;
}

// Provera identiteta vise NIJE ovde.
//
// Ranije je stajala u CLI-ju, sto je znacilo da svaki drugi klijent - WIA,
// TWAIN, aplikacija - moze da je zaobidje. Sada je u G2710Device::identify(),
// pa vazi za sve. CLI je samo jos jedan klijent, ne izuzetak.

int cmdRegdump(ITransport& transport, const SafetyGate& gate) {
    if (const Status allowed = gate.require(SafetyLevel::ReadOnly, "regdump"); !allowed) {
        reportError("regdump", allowed.error());
        return 2;
    }

    std::vector<std::byte> bank(static_cast<std::size_t>(profile::kRegisterBankLength));
    const Status status = transport.controlIn(
        static_cast<std::uint16_t>(profile::kRegisterBankBase),
        Command::RegisterRead, bank);
    if (!status) {
        reportError("controlIn(RegisterRead)", status.error());
        return 3;
    }

    for (std::size_t offset = 0; offset < bank.size(); offset += 16) {
        std::printf("%04X  ", static_cast<unsigned>(profile::kRegisterBankBase + offset));
        for (std::size_t i = 0; i < 16 && offset + i < bank.size(); ++i) {
            std::printf("%02X ", static_cast<unsigned>(bank[offset + i]));
        }
        std::putchar('\n');
    }
    return 0;
}

// Citanje senzora i statusa lampe. Sve je nivo 1 - nista se ne pomera i
// nista se ne pali. Ovo je ono sto prijatelj pokrece u H2/H3 pre nego sto
// se bilo sta drugo dozvoli.
int cmdStatus(ITransport& transport, const SafetyGate& gate) {

    rts8822::Rts8822 chip{transport, gate};

    auto executing = chip.isExecuting();
    if (!executing) {
        reportError("isExecuting", executing.error());
        return 3;
    }
    std::printf("Scan u toku           %s\n", executing.value() ? "da" : "ne");

    auto home = chip.isHeadAtHome();
    if (!home) {
        reportError("isHeadAtHome", home.error());
        return 3;
    }
    std::printf("Glava na home         %s\n", home.value() ? "da" : "ne");

    auto lamp = chip.lampStatus();
    if (!lamp) {
        reportError("lampStatus", lamp.error());
        return 3;
    }
    std::printf("Flatbed lampa         %s\n", lamp.value().flatbedOn ? "gori" : "ugasena");
    std::printf("TMA lampa             %s\n", lamp.value().tmaOn ? "gori" : "ugasena");

    auto duty = chip.lampPwmDutyCycle();
    if (!duty) {
        reportError("lampPwmDutyCycle", duty.error());
        return 3;
    }
    std::printf("PWM duty cycle        %u / 63\n", static_cast<unsigned>(duty.value()));

    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    Options options;
    if (!parse(argc, argv, &options)) {
        std::fputs(kUsage, stderr);
        return 1;
    }

    const SafetyGate gate{options.requested};
    if (gate.wasClamped()) {
        std::fprintf(stderr,
                     "napomena: trazen nivo %s, ovaj build dozvoljava %s\n",
                     toString(gate.requested()), toString(gate.effective()));
    }

    if (options.command == "info") {
        return cmdInfo(gate);
    }

    if (options.command == "capabilities") {
        return cmdCapabilities(options.json);
    }

    std::unique_ptr<TransportProvider::ScopedTestProvider> simGuard;
    if (options.transport == "sim") {
        simGuard = std::make_unique<TransportProvider::ScopedTestProvider>(
            std::make_unique<sim::SimTransportProvider>());
    } else if (options.transport != "usbscan") {
        std::fprintf(stderr, "nepoznat transport: %s\n", options.transport.c_str());
        return 1;
    }

    const DeviceRef ref = options.devicePath.empty()
                              ? DeviceRef::defaultUsbScan()
                              : DeviceRef::devicePath(options.devicePath);

    DeviceOptions deviceOptions;
    deviceOptions.safety = gate;
    deviceOptions.clientName = "g2710ctl";

    auto device = G2710Device::open(ref, deviceOptions);
    if (!device) {
        reportError("otvaranje uredjaja", device.error());
        return 3;
    }
    G2710Device& scanner = *device.value();

    // `probe` je jedina komanda koja radi i na neidentifikovanom uredjaju -
    // njena svrha je upravo da kaze STA je na drugom kraju. Sve ostalo trazi
    // potvrdjen identitet, jer salje vendor komande.
    if (options.command == "probe") {
        return cmdProbe(scanner.transport(), gate);
    }

    if (const Status identified = scanner.identify(); !identified) {
        if (identified.error().code == ErrorCode::DeviceNotFound) {
            std::fprintf(stderr,
                         "odbijeno: uredjaj na ovom portu je %04X:%04X, a ne G2710 (%04X:%04X).\n"
                         "Vendor komande se ne salju tudjem uredjaju.\n",
                         scanner.identity().vendorId, scanner.identity().productId,
                         profile::kUsbVendorId, profile::kUsbProductId);
            return 6;
        }
        reportError("identify", identified.error());
        return 4;
    }

    if (const Status started = scanner.begin(); !started) {
        if (started.error().code == ErrorCode::Busy) {
            const std::string owner = scanner.currentOwner();
            std::fprintf(stderr, "uredjaj trenutno koristi %s\n",
                         owner.empty() ? "drugi klijent" : owner.c_str());
            return 7;
        }
        reportError("begin", started.error());
        return 4;
    }

    if (options.command == "regdump") {
        return cmdRegdump(scanner.transport(), gate);
    }
    if (options.command == "status") {
        return cmdStatus(scanner.transport(), gate);
    }

    std::fputs(kUsage, stderr);
    return 1;
}
