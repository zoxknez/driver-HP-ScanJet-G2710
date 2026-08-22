// g2710ctl - dijagnosticki CLI.
//
// Sve ide kroz iste slojeve koje koriste WIA, TWAIN i aplikacija: isti
// TransportProvider, isti DeviceArbiter, isti SafetyGate. Ono sto ovde radi,
// radi i tamo - i obrnuto.

#include "device/G2710Device.h"
#include "device/SafetyLevel.h"
#include "rts8822/Lamp.h"
#include "rts8822/Rts8822.h"
#include "scan/Capabilities.h"
#include "scan/ScanPlanner.h"
#include "scan/ScanSession.h"

#include "ImageOutput.h"
#include "Qualification.h"
#include "transport/ITransportProvider.h"
#include "transport/UsbScanTransport.h"
#include "G2710Profile.generated.h"

#include "../sim/SimTransport.h"

#include <chrono>
#include <cstdarg>
#include <ctime>
#include <io.h>
#include <thread>
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
    "Komande:\n"
    "  probe    [opcije]   identitet uredjaja i konfiguracija pipe-ova\n"
    "  regdump  [opcije]   ispis registarskog bank-a\n"
    "  status   [opcije]   senzori, lampe, PWM (sve read-only)\n"
    "  scan     [opcije]   skeniraj u PNM fajl\n"
    "  qualify  [opcije]   hardverska kvalifikacija, izvestaj u JSON\n"
    "  info                ugradjeni profil i granice build-a\n"
    "  capabilities        tabela mogucnosti sa statusom validacije\n"
    "\n"
    "Opste opcije:\n"
    "  --transport <usbscan|sim>    podrazumevano usbscan\n"
    "  --device <putanja>           podrazumevano \\\\.\\Usbscan0\n"
    "  --safety-level <1..5>        zahtevani nivo; efektivni je\n"
    "                               min(BuildSafetyCeiling, zahtevani)\n"
    "  --json                       masinski citljiv izlaz (capabilities)\n"
    "\n"
    "Opcije komande scan:\n"
    "  --dpi <n>                    50 75 100 150 200 300 600 1200 2400\n"
    "  --mode <color|gray|lineart>  podrazumevano color\n"
    "  --depth <8|16>               podrazumevano 8\n"
    "  --out <putanja>              podrazumevano scan.ppm, .pgm ili .pbm\n"
    "  --region <L,G,S,V>           u pikselima na trazenoj rezoluciji\n"
    "  --gamma <broj>               1.0 je bez korekcije\n"
    "  --warmup <ms>                cekanje posle paljenja lampe; 3000\n"
    "\n"
    "Opcije komande qualify:\n"
    "  --out <putanja>              podrazumevano test-results.json\n"
    "  --safety-level 5             bez ovoga se provere iznad plafona\n"
    "                               ne pokusavaju, i to se tako prijavi\n"
    "  --only-advertised            odbij sve sto hardver nije potvrdio -\n"
    "                               ono sto bi WIA i TWAIN videli\n";

// Definisano nize, uz ostatak scan komande; deklarisano ovde jer ga parse()
// koristi.
bool parseRegion(const std::string& text, scan::ScanRegion* out);

struct Options {
    std::string command;
    std::string transport = "usbscan";
    std::wstring devicePath;
    SafetyLevel requested = SafetyLevel::ReadOnly;
    bool json = false;

    // scan
    int dpi = 300;
    image::ColorMode mode = image::ColorMode::Color;
    int depth = 8;
    std::string outputPath;
    scan::ScanRegion region;
    double gamma = 1.0;
    bool onlyAdvertised = false;
    int warmupMs = 3000;
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
        } else if (arg == "--warmup" && hasNext) {
            out->warmupMs = std::atoi(argv[++i]);
        } else if (arg == "--only-advertised") {
            out->onlyAdvertised = true;
        } else if (arg == "--dpi" && hasNext) {
            out->dpi = std::atoi(argv[++i]);
        } else if (arg == "--depth" && hasNext) {
            out->depth = std::atoi(argv[++i]);
        } else if (arg == "--out" && hasNext) {
            out->outputPath = argv[++i];
        } else if (arg == "--gamma" && hasNext) {
            out->gamma = std::atof(argv[++i]);
        } else if (arg == "--mode" && hasNext) {
            const std::string value = argv[++i];
            if (value == "color") {
                out->mode = image::ColorMode::Color;
            } else if (value == "gray") {
                out->mode = image::ColorMode::Gray;
            } else if (value == "lineart") {
                out->mode = image::ColorMode::Lineart;
            } else {
                std::fprintf(stderr, "nepoznat rezim: %s (color, gray ili lineart)\n",
                             value.c_str());
                return false;
            }
        } else if (arg == "--region" && hasNext) {
            if (!parseRegion(argv[++i], &out->region)) {
                std::fprintf(stderr,
                             "oblast se pise kao --region levo,gore,sirina,visina\n");
                return false;
            }
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

// --- scan -------------------------------------------------------------------

// Grupisan ispis: naslov pa parovi ime/vrednost poravnati u kolonu. CLI je
// prvo mesto na kome prijatelj vidi da li nesto radi, pa se cita kao izvestaj,
// ne kao dnevnik.
void printSection(const char* title) {
    std::printf("\n%s\n", title);
}

void printField(const char* name, const char* format, ...) {
    std::printf("  %-20s ", name);
    va_list args;
    va_start(args, format);
    std::vprintf(format, args);
    va_end(args);
    std::printf("\n");
}

std::string formatBytes(std::size_t bytes) {
    char buffer[64];
    if (bytes >= 1024u * 1024u) {
        std::snprintf(buffer, sizeof(buffer), "%.1f MB (%zu bajta)",
                      static_cast<double>(bytes) / (1024.0 * 1024.0), bytes);
    } else if (bytes >= 1024u) {
        std::snprintf(buffer, sizeof(buffer), "%.1f kB (%zu bajta)",
                      static_cast<double>(bytes) / 1024.0, bytes);
    } else {
        std::snprintf(buffer, sizeof(buffer), "%zu bajta", bytes);
    }
    return buffer;
}

// Da li izlaz ide u terminal.
//
// Vazno je: `\r` prepisuje red samo u terminalu. U preusmerenom izlazu bi
// svaki red ostao, pa bi prolaz od tri hiljade redova ostavio dve stotine
// kilobajta smeca umesto jedne trake.
bool stdoutIsTerminal() {
    return _isatty(_fileno(stdout)) != 0;
}

// Traka napretka.
//
// Stanje je u strukturi, ne u statickim promenljivama: dva prolaza u istom
// procesu moraju krenuti od nule, inace drugi ne bi ispisao nista.
struct Progress {
    int total = 0;
    int lastStep = -1;
    int lastDecile = -1;
    bool terminal = false;

    void begin(int totalLines) {
        total = totalLines;
        lastStep = -1;
        lastDecile = -1;
        terminal = stdoutIsTerminal();
    }

    void update(int done) {
        constexpr int kWidth = 38;
        constexpr int kSteps = 200;  // najvise toliko crtanja po prolazu

        const double fraction = total > 0 ? static_cast<double>(done) / total : 0.0;
        const int percent = static_cast<int>(fraction * 100.0);

        if (!terminal) {
            // Bez terminala `\r` ne prepisuje red, pa bi svaki red ostao.
            // Javlja se samo kada predje na sledecih deset odsto.
            const int decile = percent / 10;
            if (decile != lastDecile) {
                lastDecile = decile;
                std::printf("  %3d%%  %d/%d redova\n", percent, done, total);
            }
            return;
        }

        const int step = total > 0 ? (done * kSteps) / total : kSteps;
        if (step == lastStep) {
            return;
        }
        lastStep = step;

        const int filled = static_cast<int>(fraction * kWidth);
        std::printf("\r  [");
        for (int i = 0; i < kWidth; ++i) {
            std::putchar(i < filled ? '#' : '.');
        }
        std::printf("] %3d%%  %d/%d redova", percent, done, total);
        std::fflush(stdout);
    }

    void finish(int done) {
        update(done);
        if (terminal) {
            std::printf("\n");
        }
    }

    // Zatvori traku pre poruke o gresci, da se ne slepe u isti red.
    void interrupt() const {
        if (terminal) {
            std::printf("\n");
        }
    }
};

bool parseRegion(const std::string& text, scan::ScanRegion* out) {
    int values[4] = {0, 0, 0, 0};
    std::size_t start = 0;

    for (int i = 0; i < 4; ++i) {
        const std::size_t comma = text.find(',', start);
        const bool last = (i == 3);

        // Cetvrti deo nema zarez iza sebe, prva tri ga moraju imati.
        if (last != (comma == std::string::npos)) {
            return false;
        }

        const std::string part =
            text.substr(start, last ? std::string::npos : comma - start);
        if (part.empty()) {
            return false;
        }
        values[i] = std::atoi(part.c_str());
        if (!last) {
            start = comma + 1;
        }
    }

    out->left = values[0];
    out->top = values[1];
    out->width = values[2];
    out->height = values[3];
    return true;
}

int cmdScan(G2710Device& scanner, const Options& options) {
    using namespace g2710::scan;

    // Planiranje je cist racun i ne dira uredjaj; greske se vide pre nego sto
    // se bilo sta pokrene.
    ScanRequest request;
    request.resolution = options.dpi;
    request.colorMode = options.mode;
    request.depth = options.depth;
    request.region = options.region;
    request.allowUnqualified = !options.onlyAdvertised;

    auto planned = planScan(request);
    if (!planned) {
        std::fprintf(stderr, "plan: %s", toString(planned.error().code));
        if (planned.error().context[0] != '\0') {
            std::fprintf(stderr, " (%s)", planned.error().context);
        }
        std::fputc('\n', stderr);
        if (options.onlyAdvertised) {
            std::fprintf(stderr,
                         "\nRezim --only-advertised nudi samo hardverski potvrdjene\n"
                         "rezolucije, a nijedna to jos nije. Izostavi taj prekidac za\n"
                         "dijagnosticko skeniranje.\n");
        }
        return 8;
    }
    const ScanPlan& plan = planned.value();

    const ResolutionCapability* capability = findResolution(options.dpi);
    const bool qualified = capability != nullptr && capability->advertisable();

    printSection("Plan skeniranja");
    printField("Trazeno", "%d dpi, %s, %d bita po kanalu", plan.requestedResolution,
               toString(options.mode), options.depth);
    printField("Skenira se na", "%d dpi (%s)", plan.nativeResolution,
               plan.resize == ResizeType::None ? "native" : "pa se smanjuje");
    printField("Oblast", "%d x %d px na (%d, %d)", plan.requestedRegion.width,
               plan.requestedRegion.height, plan.requestedRegion.left, plan.requestedRegion.top);
    if (plan.resize != ResizeType::None) {
        printField("Na hardveru", "%d x %d px", plan.nativeRegion.width, plan.nativeRegion.height);
    }
    printField("Red na hardveru", "%zu bajta", plan.hardwareLine.bytesPerLine);
    printField("Red na izlazu", "%zu bajta", plan.outputLine.bytesPerLine);
    printField("Poravnanje kanala", "%s",
               plan.tableMode != image::ColorMode::Color
                   ? "nije potrebno (jedan kanal)"
                   : (plan.useHardwareAlignment ? "hardversko" : "softversko"));
    if (!plan.useHardwareAlignment && plan.softwareLineDistance > 0) {
        printField("Razmak redova", "%d redova, lookahead %d", plan.softwareLineDistance,
                   plan.alignmentLookahead);
    }
    printField("Timing profil", "%d", plan.timingIndex);
    printField("Motorna kriva", "%s",
               plan.motorCurveIndex >= 0 ? std::to_string(plan.motorCurveIndex).c_str()
                                         : "nema u tabeli");

    ScanOptions scanOptions;
    if (options.gamma > 0.0 && options.gamma != 1.0) {
        scanOptions.gamma = image::makeGammaTable(options.gamma);
    }

    rts8822::RegisterFile registers{scanner.transport()};
    ScanSession session{registers, scanner.safety(), plan, std::move(scanOptions)};

    // Lampa mora goreti pre prolaza; sesija to i proverava, ali paljenje je
    // posao pozivaoca - isto kao na uredjaju, gde Scan_Start pretpostavlja da
    // je Lamp_Warmup vec prosao.
    printSection("Lampa");
    rts8822::Lamp lamp{registers, scanner.safety()};

    if (const Status lit = lamp.setLamp(rts8822::LampKind::Flatbed, true); !lit) {
        if (lit.error().code == ErrorCode::SafetyViolation) {
            std::fprintf(stderr,
                         "odbijeno: paljenje lampe trazi nivo 2, a efektivni je %s.\n",
                         toString(scanner.safety().effective()));
            return 10;
        }
        reportError("paljenje lampe", lit.error());
        return 10;
    }
    if (const Status pwm = lamp.setupPwm(rts8822::LampKind::Flatbed); !pwm) {
        reportError("PWM lampe", pwm.error());
        return 10;
    }

    rts8822::Rts8822 chip{scanner.transport(), scanner.safety()};
    const auto lampState = chip.lampStatus();
    const auto duty = chip.lampPwmDutyCycle();

    printField("Flatbed", "%s",
               lampState && lampState.value().flatbedOn ? "gori" : "NE GORI");
    printField("PWM duty", "%s",
               duty ? (duty.value() == 0 ? "0 (bez ogranicenja, kao u profilu)"
                                         : std::to_string(duty.value()).c_str())
                    : "nepoznat");

    if (options.warmupMs > 0) {
        printField("Zagrevanje", "%d ms", options.warmupMs);
        std::this_thread::sleep_for(std::chrono::milliseconds(options.warmupMs));
    } else {
        printField("Zagrevanje", "preskoceno (--warmup 0)");
    }
    printField("Stabilizacija", "%s",
               "nije merena - Lamp_PWM_CheckStable trazi GetOneLineInfo");

    printSection("Obrada");
    printField("Shading", "%s",
               session.shadingApplied()
                   ? "primenjen"
                   : "NIJE primenjen - kalibracija nije pokrenuta");
    printField("Gamma", "%s", session.gammaApplied() ? "primenjena" : "nije primenjena");
    printField("Sivo iz", "%s",
               options.mode == image::ColorMode::Color ? "-" : "crvenog kanala (kao referenca)");

    if (!qualified) {
        std::printf("\n  UPOZORENJE   %d dpi nije hardverski potvrdjeno.\n", options.dpi);
        std::printf("               WIA i TWAIN ovu rezoluciju ne nude. Vidi docs/STATUS.md.\n");
        if (capability != nullptr && capability->note[0] != '\0') {
            std::printf("               %s\n", capability->note);
        }
    }

    // Podrazumevano ime prati rezim, da se .ppm i .pgm ne pomesaju.
    std::string outputPath = options.outputPath;
    if (outputPath.empty()) {
        outputPath = std::string("scan.") + cli::pnmExtension(options.mode);
    }

    auto writer = cli::PnmWriter::create(outputPath, options.mode, options.depth,
                                         plan.requestedRegion.width, plan.requestedRegion.height);
    if (!writer) {
        reportError("izlazni fajl", writer.error());
        return 9;
    }
    std::unique_ptr<cli::PnmWriter> output{writer.value()};

    if (const Status begun = session.begin(); !begun) {
        if (begun.error().code == ErrorCode::SafetyViolation) {
            std::fprintf(stderr,
                         "\nodbijeno: skeniranje trazi nivo 5, a efektivni je %s.\n"
                         "Dodaj --safety-level 5 (i proveri BuildSafetyCeiling kroz `info`).\n",
                         toString(scanner.safety().effective()));
            return 10;
        }
        reportError("pokretanje prolaza", begun.error());
        return 10;
    }

    printSection("Skeniranje");
    std::printf("  u %s\n", outputPath.c_str());

    const auto startTime = std::chrono::steady_clock::now();
    std::vector<std::uint8_t> line(session.outputBytesPerLine(), 0);

    Progress progress;
    progress.begin(session.expectedOutputLines());

    for (;;) {
        auto more = session.nextLine(line, scanner.cancellation());
        if (!more) {
            progress.interrupt();
            reportError("citanje reda", more.error());
            (void)session.abort();
            return 11;
        }
        if (!more.value()) {
            break;
        }
        if (const Status written = output->writeLine(line); !written) {
            progress.interrupt();
            reportError("upis reda", written.error());
            (void)session.abort();
            return 12;
        }

        progress.update(session.statistics().outputLinesProduced);
    }
    progress.finish(session.statistics().outputLinesProduced);

    const Status closed = output->close();
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - startTime);

    const ScanStatistics& stats = session.statistics();

    printSection("Rezultat");
    printField("Fajl", "%s   %s", outputPath.c_str(), output->formatName());
    printField("Redova upisano", "%d od %d", output->linesWritten(), session.expectedOutputLines());
    printField("Hardverskih redova", "%d", stats.hardwareLinesRead);
    if (stats.alignmentLinesConsumed > 0) {
        printField("Pojelo poravnanje", "%d redova", stats.alignmentLinesConsumed);
    }
    if (stats.resampleLinesConsumed > 0) {
        printField("Pojelo smanjivanje", "%d redova", stats.resampleLinesConsumed);
    }
    printField("Procitano", "%s", formatBytes(stats.bytesRead).c_str());
    printField("Trajanje", "%.1f s", static_cast<double>(elapsed.count()) / 1000.0);

    if (!closed) {
        std::printf("\n");
        reportError("zatvaranje fajla", closed.error());
        return 13;
    }

    std::printf("\n");
    return 0;
}

// --- qualify ----------------------------------------------------------------

// Trenutak u ISO obliku. Modul kvalifikacije ga ne izmislja sam - da izmislja,
// testovi mu ne bi bili deterministicki.
std::string nowIso8601() {
    const std::time_t now = std::time(nullptr);
    std::tm local{};
    if (localtime_s(&local, &now) != 0) {
        return "nepoznato";
    }
    char buffer[32] = {};
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%S", &local);
    return buffer;
}

// Oznaka uredjaja za izvestaj i za kes kalibracije.
std::string deviceIdentityString(const DeviceIdentity& identity) {
    char buffer[32] = {};
    std::snprintf(buffer, sizeof(buffer), "%04X-%04X", identity.vendorId, identity.productId);
    return buffer;
}

int cmdQualify(G2710Device& scanner, const Options& options) {
    const auto started = std::chrono::steady_clock::now();

    printSection("Hardverska kvalifikacija");
    printField("Uredjaj", "%04X:%04X", scanner.identity().vendorId,
               scanner.identity().productId);
    printField("Transport", "%s", scanner.transport().name());
    printField("Plafon build-a", "%s", toString(scanner.safety().ceiling()));
    printField("Efektivni nivo", "%s", toString(scanner.safety().effective()));

    if (toInt(scanner.safety().effective()) < toInt(SafetyLevel::FullScan)) {
        std::printf("\n  Provere iznad efektivnog nivoa se NE pokusavaju. To nije\n");
        std::printf("  greska - paket sa nizim plafonom namerno ne dodiruje motor.\n");
    }

    const std::vector<cli::CheckResult> results = cli::runQualification(scanner);
    const cli::QualificationSummary summary = cli::summarise(results);

    printSection("Rezultati");
    for (const cli::CheckResult& result : results) {
        const char* mark = result.outcome == cli::CheckOutcome::Pass   ? "  OK  "
                           : result.outcome == cli::CheckOutcome::Fail ? " PAO  "
                           : result.outcome == cli::CheckOutcome::AsksTheUser ? " ???  "
                                                                             : "  --  ";
        std::printf("  [%s] %-7s %-28s %s\n", mark, result.id.c_str(), result.name.c_str(),
                    result.detail.empty() ? result.question.c_str() : result.detail.c_str());
    }

    printSection("Sazetak");
    printField("Proslo", "%d", summary.passed);
    printField("Palo", "%d", summary.failed);
    printField("Blokirano plafonom", "%d", summary.blocked);
    printField("Ceka kod", "%d", summary.notImplemented);
    printField("Pitanja za korisnika", "%d", summary.questions);

    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - started);
    printField("Trajanje", "%.1f s", static_cast<double>(elapsed.count()) / 1000.0);

    if (summary.questions > 0) {
        std::printf("\n  Pitanja na koja masina ne moze da odgovori:\n");
        for (const cli::CheckResult& result : results) {
            if (result.outcome == cli::CheckOutcome::AsksTheUser) {
                std::printf("    %-7s %s\n", result.id.c_str(), result.question.c_str());
            }
        }
        std::printf("\n  Odgovore upisuje wizard; ovaj alat ih samo prijavljuje.\n");
    }

    // Izvestaj se pise UVEK, i kad nesto padne - tada je najpotrebniji.
    const std::string report =
        cli::formatReport(results, deviceIdentityString(scanner.identity()), nowIso8601(),
                          scanner.safety());

    std::string reportPath = options.outputPath;
    if (reportPath.empty()) {
        reportPath = "test-results.json";
    }

    std::FILE* file = nullptr;
    if (const errno_t error = fopen_s(&file, reportPath.c_str(), "wb");
        error != 0 || file == nullptr) {
        std::fprintf(stderr, "\nne mogu da upisem %s\n", reportPath.c_str());
        return 14;
    }
    std::fwrite(report.data(), 1, report.size(), file);
    std::fclose(file);

    printSection("Izvestaj");
    printField("Fajl", "%s", reportPath.c_str());
    std::printf("\n  Prekopirajte ga u qualification/ u repozitorijumu; STATUS.md\n");
    std::printf("  tada prikazuje treci stubac umesto \"ceka\".\n\n");

    // Izlazni kod govori ishod, da ga wizard i skripte ne moraju parsirati.
    return summary.failed == 0 ? 0 : 15;
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
    if (options.command == "scan") {
        return cmdScan(scanner, options);
    }
    if (options.command == "qualify") {
        return cmdQualify(scanner, options);
    }

    std::fputs(kUsage, stderr);
    return 1;
}
