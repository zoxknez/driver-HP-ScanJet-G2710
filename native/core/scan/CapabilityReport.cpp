#include "CapabilityReport.h"

#include "../device/G2710Profile.generated.h"
#include "Capabilities.h"
#include "ScanPlanner.h"

#include <cstdarg>
#include <cstdio>

namespace g2710::scan {
namespace {

// JSON niska sa izbegnutim navodnicima i obrnutim kosim crtama.
//
// Napomene u tabeli su nasi literali i danas ne sadrze nista od toga - ali
// izvestaj koji se lomi zato sto je neko dopisao navodnik u komentar je greska
// koja se otkriva tek kada parser padne kod korisnika.
std::string escape(const char* text) {
    std::string out;
    for (const char* it = text; it != nullptr && *it != '\0'; ++it) {
        switch (*it) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:   out += *it;    break;
        }
    }
    return out;
}

std::string format(const char* pattern, ...) {
    char line[512];
    va_list arguments;
    va_start(arguments, pattern);
    std::vsnprintf(line, sizeof(line), pattern, arguments);
    va_end(arguments);
    return line;
}

}  // namespace

std::string capabilitiesJson() {
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

        // Dijagnostika sme da vidi i ono sto se ne oglasava; `advertisable` u
        // izlazu je taj koji kaze sta se sme ponuditi korisniku.
        request.allowUnqualified = true;

        Row row{&capability, false, {}};
        if (auto plan = planScan(request)) {
            row.planned = true;
            row.plan = plan.value();
        }
        rows.push_back(row);
    }

    const std::vector<int> advertisable = advertisableResolutions();

    std::string out;
    out += "{\n";
    out += format("  \"device\": \"%04X:%04X\",\n", profile::kUsbVendorId,
                  profile::kUsbProductId);
    out += "  \"resolutions\": [\n";

    for (std::size_t i = 0; i < rows.size(); ++i) {
        const Row& row = rows[i];
        out += format("    {\"dpi\": %d, \"origin\": \"%s\", \"level\": \"%s\", "
                      "\"sourceDpi\": %d, \"nativeDpi\": %d, \"resize\": \"%s\", "
                      "\"alignment\": \"%s\", \"advertisable\": %s, \"note\": \"%s\"}%s\n",
                      row.capability->dpi, toString(row.capability->origin),
                      toString(row.capability->level), row.capability->sourceDpi,
                      row.planned ? row.plan.nativeResolution : 0,
                      row.planned ? toString(row.plan.resize) : "-",
                      row.planned ? (row.plan.useHardwareAlignment ? "hardware" : "software")
                                  : "-",
                      row.capability->advertisable() ? "true" : "false",
                      escape(row.capability->note).c_str(),
                      i + 1 == rows.size() ? "" : ",");
    }

    out += "  ],\n";
    out += "  \"depths\": [\n";

    const auto depths = depthCapabilities();
    for (std::size_t i = 0; i < depths.size(); ++i) {
        out += format("    {\"bits\": %d, \"level\": \"%s\", \"note\": \"%s\"}%s\n",
                      depths[i].bits, toString(depths[i].level),
                      escape(depths[i].note).c_str(),
                      i + 1 == depths.size() ? "" : ",");
    }

    out += "  ],\n";
    out += "  \"advertisable\": [";
    for (std::size_t i = 0; i < advertisable.size(); ++i) {
        out += format("%s%d", i == 0 ? "" : ", ", advertisable[i]);
    }
    out += "]\n}\n";
    return out;
}

}  // namespace g2710::scan
