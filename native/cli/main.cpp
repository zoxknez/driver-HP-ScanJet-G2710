// g2710ctl - dijagnosticki CLI.
//
// Sve ide kroz iste slojeve koje koriste WIA, TWAIN i aplikacija: isti
// TransportProvider, isti DeviceArbiter, isti SafetyGate. Ono sto ovde radi,
// radi i tamo - i obrnuto.

#include "device/DeviceArbiter.h"
#include "device/SafetyLevel.h"
#include "rts8822/Rts8822.h"
#include "transport/ITransportProvider.h"
#include "transport/UsbScanTransport.h"
#include "G2710Profile.generated.h"

#include "../sim/SimTransport.h"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>

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
};

bool parse(int argc, char** argv, Options* out) {
    if (argc < 2) {
        return false;
    }
    out->command = argv[1];

    for (int i = 2; i < argc; ++i) {
        const std::string arg = argv[i];
        const bool hasNext = (i + 1) < argc;

        if (arg == "--transport" && hasNext) {
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

// Nijedna vendor komanda ne sme otici uredjaju koji nije G2710.
//
// \\.\Usbscan0 je deljeno ime - na masini moze biti bilo koji uredjaj vezan za
// usbscan.sys. Na razvojnoj masini je to bio HP LaserJet MFP M139-M142, cija
// je skener funkcija takodje pod klasom Image. Vendor request 0x04 sa G2710
// registarskom semantikom poslat tudjem uredjaju je tacno ona vrsta poteza
// koju ovaj projekat inace zabranjuje sebi.
//
// Citanje descriptor-a i pipe konfiguracije je bezbedno (read-only, standardni
// USB); vendor transferi nisu. Zato ova provera stoji izmedju njih.
//
// Pripada G2710Device u G2710-4; ovde je jer je potreba stvarna vec sada.
Status ensureIsG2710(ITransport& transport) {
    auto* usbscan = dynamic_cast<UsbScanTransport*>(&transport);
    if (usbscan == nullptr) {
        return ok();  // simulator ili replay
    }

    auto id = usbscan->identity();
    if (!id) {
        return id.error();
    }
    if (id.value().vendorId != profile::kUsbVendorId ||
        id.value().productId != profile::kUsbProductId) {
        std::fprintf(stderr,
                     "odbijeno: uredjaj na ovom portu je %04X:%04X, a ne G2710 (%04X:%04X).\n"
                     "Vendor komande se ne salju tudjem uredjaju.\n",
                     id.value().vendorId, id.value().productId,
                     profile::kUsbVendorId, profile::kUsbProductId);
        return fail(ErrorCode::DeviceNotFound, "ensureIsG2710");
    }
    return ok();
}

int cmdRegdump(ITransport& transport, const SafetyGate& gate) {
    if (const Status allowed = gate.require(SafetyLevel::ReadOnly, "regdump"); !allowed) {
        reportError("regdump", allowed.error());
        return 2;
    }
    if (const Status identified = ensureIsG2710(transport); !identified) {
        return 6;
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
    if (const Status identified = ensureIsG2710(transport); !identified) {
        return 6;
    }

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

    auto transport = TransportProvider::create(ref);
    if (!transport) {
        reportError("otvaranje uredjaja", transport.error());
        return 3;
    }

    if (options.command == "probe") {
        return cmdProbe(*transport.value(), gate);
    }
    if (options.command == "regdump") {
        return cmdRegdump(*transport.value(), gate);
    }
    if (options.command == "status") {
        return cmdStatus(*transport.value(), gate);
    }

    std::fputs(kUsage, stderr);
    return 1;
}
