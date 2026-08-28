// ABI se menja NAMERNO, nikada usput.
//
// Sa druge strane granice stoji P/Invoke koji se ne prevodi zajedno sa ovim
// kodom. Promena koja ovde prodje kao bezopasna - dodato polje u sredini
// strukture, preuredjen enum, funkcija izvezena a ne deklarisana - tamo postaje
// "entry point not found" ili tiho procitano smece, i to kod korisnika.
//
// Zato ovaj fajl poredi tri stvari koje inace niko ne poredi:
//
//   1. spisak u G2710.Native.def  <->  deklaracije u g2710_abi.h
//   2. velicine i rasporedi struktura  <->  zapamcene vrednosti
//   3. brojevi u enum-ima  <->  zapamcene vrednosti
//
// Kada promena JESTE namerna, ovaj test se azurira zajedno sa zaglavljem i
// verzijom ABI-ja. To je i poenta: azuriranje je svestan korak.

#include "g2710_abi.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <set>
#include <string>
#include <vector>

namespace {

std::string readFile(const char* path) {
    std::ifstream file(path, std::ios::binary);
    EXPECT_TRUE(file.good()) << "ne mogu da procitam " << path;
    return std::string((std::istreambuf_iterator<char>(file)),
                       std::istreambuf_iterator<char>());
}

std::string trim(const std::string& text) {
    const auto first = text.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return {};
    }
    const auto last = text.find_last_not_of(" \t\r\n");
    return text.substr(first, last - first + 1);
}

// Imena iz EXPORTS sekcije .def fajla.
std::set<std::string> exportedNames() {
    const std::string text = readFile(G2710_ABI_DEF_PATH);
    std::set<std::string> names;

    bool inExports = false;
    std::size_t start = 0;
    while (start <= text.size()) {
        const std::size_t end = text.find('\n', start);
        const std::string line =
            trim(text.substr(start, end == std::string::npos ? std::string::npos
                                                             : end - start));
        start = end == std::string::npos ? text.size() + 1 : end + 1;

        if (line.empty() || line[0] == ';') {
            continue;
        }
        if (line.rfind("EXPORTS", 0) == 0) {
            inExports = true;
            continue;
        }
        if (inExports) {
            names.insert(line);
        }
    }
    return names;
}

// Imena funkcija koje zaglavlje deklarise. Trazi se `G2710_CALL <ime>(`.
std::set<std::string> declaredNames() {
    const std::string text = readFile(G2710_ABI_HEADER_PATH);
    std::set<std::string> names;

    const std::string marker = "G2710_CALL ";
    std::size_t at = 0;
    while ((at = text.find(marker, at)) != std::string::npos) {
        std::size_t begin = at + marker.size();
        at = begin;

        // Pokazivaci na funkcije se deklarisu kao `(G2710_CALL* ime)` i nisu
        // izvezeni simboli - marker im ne odgovara jer nema razmak posle.
        std::size_t end = begin;
        while (end < text.size() &&
               (std::isalnum(static_cast<unsigned char>(text[end])) != 0 ||
                text[end] == '_')) {
            ++end;
        }
        if (end < text.size() && text[end] == '(' && end > begin) {
            names.insert(text.substr(begin, end - begin));
        }
    }
    return names;
}

// Imena polja jedne strukture, redom kojim su deklarisana.
//
// Zasto se cita TEKST zaglavlja umesto da se koristi offsetof: polje ubaceno u
// SREDINU strukture cesto padne u postojeci padding, pa se nijedan offset ne
// promeni i nijedna velicina ne poraste. Izmereno - bas to se desilo pri
// mutacionoj proveri ovog testa. C++ ne ume da nabroji clanove strukture, pa
// je zaglavlje jedini izvor koji to zna.
std::vector<std::string> memberNames(const std::string& structName) {
    const std::string text = readFile(G2710_ABI_HEADER_PATH);

    const std::string opener = "typedef struct " + structName + " {";
    const std::size_t begin = text.find(opener);
    EXPECT_NE(std::string::npos, begin) << "nema strukture " << structName;
    if (begin == std::string::npos) {
        return {};
    }
    const std::size_t end = text.find("} " + structName + ";", begin);
    EXPECT_NE(std::string::npos, end) << "struktura " << structName << " nije zatvorena";

    std::vector<std::string> members;
    std::size_t start = begin + opener.size();
    bool inComment = false;

    while (start < end) {
        const std::size_t stop = text.find('\n', start);
        std::string line = trim(text.substr(start, std::min(stop, end) - start));
        start = stop == std::string::npos ? end : stop + 1;

        // Komentari se preskacu; blok komentar ume da se prostire kroz vise
        // redova i unutar njega ima tacaka-zapeta koje bi prosle kao polja.
        if (inComment) {
            if (line.find("*/") != std::string::npos) {
                inComment = false;
            }
            continue;
        }
        if (line.rfind("/*", 0) == 0) {
            if (line.find("*/") == std::string::npos) {
                inComment = true;
            }
            continue;
        }
        const std::size_t semicolon = line.find(';');
        if (semicolon == std::string::npos) {
            continue;
        }
        std::string declaration = trim(line.substr(0, semicolon));

        // Poslednja rec pre tacke-zapete je ime polja; zvezdice idu uz tip.
        const std::size_t space = declaration.find_last_of(" 	*");
        if (space != std::string::npos) {
            declaration = declaration.substr(space + 1);
        }
        if (!declaration.empty()) {
            members.push_back(declaration);
        }
    }
    return members;
}

}  // namespace

// =============================================================================
// 1. Izvezeno i deklarisano moraju biti isto
// =============================================================================

TEST(AbiStability, EverythingExportedIsAlsoDeclared) {
    const std::set<std::string> exported = exportedNames();
    const std::set<std::string> declared = declaredNames();

    ASSERT_FALSE(exported.empty()) << "EXPORTS sekcija nije procitana";
    ASSERT_FALSE(declared.empty()) << "deklaracije nisu procitane";

    std::vector<std::string> orphans;
    std::set_difference(exported.begin(), exported.end(), declared.begin(),
                        declared.end(), std::back_inserter(orphans));

    // Izvezen a nedeklarisan simbol je funkcija bez ugovora: .NET je moze
    // pozvati, a niko ne zna sta ocekuje.
    EXPECT_TRUE(orphans.empty())
        << "izvezeno ali nije u zaglavlju: " << [&] {
               std::string list;
               for (const auto& name : orphans) {
                   list += name + " ";
               }
               return list;
           }();
}

TEST(AbiStability, EverythingDeclaredIsAlsoExported) {
    const std::set<std::string> exported = exportedNames();
    const std::set<std::string> declared = declaredNames();

    std::vector<std::string> missing;
    std::set_difference(declared.begin(), declared.end(), exported.begin(),
                        exported.end(), std::back_inserter(missing));

    // Deklarisan a neizvezen simbol je "entry point not found" - kod korisnika,
    // pri prvom pozivu, bez ijedne poruke o tome sta nedostaje.
    EXPECT_TRUE(missing.empty())
        << "u zaglavlju ali nije izvezeno: " << [&] {
               std::string list;
               for (const auto& name : missing) {
                   list += name + " ";
               }
               return list;
           }();
}

// =============================================================================
// 2. Rasporedi struktura
// =============================================================================

// Brojevi ispod su ZAPAMCENI, ne izracunati. Da se racunaju iz istih tipova,
// test bi se slagao sam sa sobom i ne bi merio nista.

TEST(AbiStability, OpenOptionsLayoutIsUnchanged) {
    EXPECT_EQ(0u, offsetof(g2710_open_options, size));
    EXPECT_EQ(4u, offsetof(g2710_open_options, transport));
    EXPECT_EQ(8u, offsetof(g2710_open_options, requested_safety_level));

    // Pokazivac je poravnat na 8 na x64 i na 4 na x86, pa se ostatak razlikuje
    // po arhitekturi. Obe se isporucuju, pa se obe i pamte.
    if constexpr (sizeof(void*) == 8) {
        EXPECT_EQ(16u, offsetof(g2710_open_options, client_name));
        EXPECT_EQ(24u, offsetof(g2710_open_options, acquire_timeout_ms));
        EXPECT_EQ(28u, offsetof(g2710_open_options, record_trace));
        EXPECT_EQ(32u, sizeof(g2710_open_options));
    } else {
        EXPECT_EQ(12u, offsetof(g2710_open_options, client_name));
        EXPECT_EQ(16u, offsetof(g2710_open_options, acquire_timeout_ms));
        EXPECT_EQ(20u, offsetof(g2710_open_options, record_trace));
        EXPECT_EQ(24u, sizeof(g2710_open_options));
    }
}

TEST(AbiStability, ScanRequestLayoutIsUnchanged) {
    EXPECT_EQ(0u, offsetof(g2710_scan_request, size));
    EXPECT_EQ(4u, offsetof(g2710_scan_request, resolution));
    EXPECT_EQ(8u, offsetof(g2710_scan_request, color_mode));
    EXPECT_EQ(12u, offsetof(g2710_scan_request, bits_per_channel));
    EXPECT_EQ(16u, offsetof(g2710_scan_request, left));
    EXPECT_EQ(20u, offsetof(g2710_scan_request, top));
    EXPECT_EQ(24u, offsetof(g2710_scan_request, width));
    EXPECT_EQ(28u, offsetof(g2710_scan_request, height));

    // double trazi poravnanje na 8 i na x86 i na x64 pod MSVC.
    EXPECT_EQ(32u, offsetof(g2710_scan_request, gamma));
    EXPECT_EQ(40u, offsetof(g2710_scan_request, allow_unqualified));
    EXPECT_EQ(48u, sizeof(g2710_scan_request));
}

TEST(AbiStability, ScanInfoLayoutIsUnchanged) {
    EXPECT_EQ(0u, offsetof(g2710_scan_info, size));
    EXPECT_EQ(4u, offsetof(g2710_scan_info, width_pixels));
    EXPECT_EQ(8u, offsetof(g2710_scan_info, lines));
    EXPECT_EQ(12u, offsetof(g2710_scan_info, bits_per_channel));
    EXPECT_EQ(16u, offsetof(g2710_scan_info, channels));
    EXPECT_EQ(20u, offsetof(g2710_scan_info, bytes_per_line));
    EXPECT_EQ(24u, offsetof(g2710_scan_info, native_resolution));
    EXPECT_EQ(28u, offsetof(g2710_scan_info, shading_applied));
    EXPECT_EQ(32u, sizeof(g2710_scan_info));
}

// =============================================================================
// 2b. Spisak polja
//
// Offset-i sami po sebi NE hvataju polje ubaceno u padding. Zato se i spisak
// imena drzi zakljucanim.
// =============================================================================

TEST(AbiStability, OpenOptionsHasExactlyTheseFields) {
    const std::vector<std::string> expected = {
        "size", "transport", "requested_safety_level",
        "client_name", "acquire_timeout_ms", "record_trace",
    };
    EXPECT_EQ(expected, memberNames("g2710_open_options"));
}

TEST(AbiStability, ScanRequestHasExactlyTheseFields) {
    const std::vector<std::string> expected = {
        "size", "resolution", "color_mode", "bits_per_channel",
        "left", "top", "width", "height", "gamma", "allow_unqualified",
    };
    EXPECT_EQ(expected, memberNames("g2710_scan_request"));
}

TEST(AbiStability, ScanInfoHasExactlyTheseFields) {
    const std::vector<std::string> expected = {
        "size", "width_pixels", "lines", "bits_per_channel", "channels",
        "bytes_per_line", "native_resolution", "shading_applied",
    };
    EXPECT_EQ(expected, memberNames("g2710_scan_info"));
}

// =============================================================================
// 3. Brojevi u enum-ima
// =============================================================================

TEST(AbiStability, StatusCodesKeepTheirNumbers) {
    // .NET ih poredi kao konstante. Preuredjivanje bi tiho pretvorilo
    // "otkazano" u "zauzeto".
    EXPECT_EQ(0, static_cast<int>(G2710_STATUS_OK));
    EXPECT_EQ(1, static_cast<int>(G2710_STATUS_NOT_OPEN));
    EXPECT_EQ(2, static_cast<int>(G2710_STATUS_TIMEOUT));
    EXPECT_EQ(3, static_cast<int>(G2710_STATUS_SHORT_TRANSFER));
    EXPECT_EQ(4, static_cast<int>(G2710_STATUS_STALLED));
    EXPECT_EQ(5, static_cast<int>(G2710_STATUS_CANCELLED));
    EXPECT_EQ(6, static_cast<int>(G2710_STATUS_TRANSPORT_LOST));
    EXPECT_EQ(7, static_cast<int>(G2710_STATUS_DEVICE_NOT_FOUND));
    EXPECT_EQ(8, static_cast<int>(G2710_STATUS_DEVICE_ERROR));
    EXPECT_EQ(9, static_cast<int>(G2710_STATUS_BUSY));
    EXPECT_EQ(10, static_cast<int>(G2710_STATUS_SAFETY_VIOLATION));
    EXPECT_EQ(11, static_cast<int>(G2710_STATUS_NOT_IMPLEMENTED));
    EXPECT_EQ(12, static_cast<int>(G2710_STATUS_INVALID_ARGUMENT));
    EXPECT_EQ(13, static_cast<int>(G2710_STATUS_INVALID_STATE));
    EXPECT_EQ(14, static_cast<int>(G2710_STATUS_INTERNAL));
}

TEST(AbiStability, DeviceStatesKeepTheirNumbers) {
    EXPECT_EQ(0, static_cast<int>(G2710_STATE_DISCONNECTED));
    EXPECT_EQ(3, static_cast<int>(G2710_STATE_IDLE));
    EXPECT_EQ(7, static_cast<int>(G2710_STATE_SCANNING));

    // TransportLost je vazniji od ostalih: .NET na njega mora reagovati
    // obaveznim HOME-om, pa promena bas ovog broja tiho gasi tu obavezu.
    EXPECT_EQ(9, static_cast<int>(G2710_STATE_TRANSPORT_LOST));
    EXPECT_EQ(11, static_cast<int>(G2710_STATE_EMERGENCY_STOPPED));
}

TEST(AbiStability, ColorModesAndTransportsKeepTheirNumbers) {
    EXPECT_EQ(0, static_cast<int>(G2710_COLOR));
    EXPECT_EQ(1, static_cast<int>(G2710_GRAY));
    EXPECT_EQ(2, static_cast<int>(G2710_LINEART));

    EXPECT_EQ(0, static_cast<int>(G2710_TRANSPORT_USBSCAN));
    EXPECT_EQ(1, static_cast<int>(G2710_TRANSPORT_SIM));
}

TEST(AbiStability, TheVersionIsBumpedWhenTheContractChanges) {
    // Ako neki test iznad padne zato sto je promena NAMERNA, ovaj broj se menja
    // u istom commit-u. Bez toga .NET ne moze razlikovati stari DLL od novog.
    //
    // 1.0 -> 1.1: dodat g2710_capabilities. Dodavanje na KRAJ ugovora ne kvari
    // starijeg pozivaoca - on tu funkciju prosto ne zove - pa raste manji broj.
    // Da je promenjen raspored strukture ili broj u enum-u, rastao bi veci.
    EXPECT_EQ(1, G2710_ABI_VERSION_MAJOR);
    EXPECT_EQ(1, G2710_ABI_VERSION_MINOR);
}
