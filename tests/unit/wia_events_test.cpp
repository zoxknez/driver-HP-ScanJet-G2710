// Dugmad: kod i INF moraju govoriti isto.
//
// STI servis povezuje pritisak dugmeta sa registrovanim rukovaocem preko
// GUID-a iz Events podkljuca. Ako GUID u INF-u i GUID koji vraca
// GetNotificationData nisu isti, dugme "ne radi" - a to se vidi tek kada ga
// neko pritisne, dakle u H10, na daljinu.
//
// Zato je ovo test, ne komentar.

#include "WiaEvents.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <set>
#include <string>

#ifndef G2710_INF_PATH
#error "G2710_INF_PATH mora biti definisan u build sistemu"
#endif

using namespace g2710::wia;

namespace {

std::string toUpper(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return text;
}

std::string formatGuid(const GUID& guid) {
    char buffer[64] = {};
    std::snprintf(buffer, sizeof(buffer),
                  "{%08lX-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X}",
                  static_cast<unsigned long>(guid.Data1), guid.Data2, guid.Data3,
                  guid.Data4[0], guid.Data4[1], guid.Data4[2], guid.Data4[3],
                  guid.Data4[4], guid.Data4[5], guid.Data4[6], guid.Data4[7]);
    return buffer;
}

// Cita `G2710.XxxEventGuid = "{...}"` iz [Strings] sekcije INF-a.
std::string infGuid(const std::string& key) {
    std::ifstream file(G2710_INF_PATH);
    if (!file) {
        return {};
    }

    std::string line;
    while (std::getline(file, line)) {
        if (line.find(key) == std::string::npos) {
            continue;
        }
        const std::size_t open = line.find('{');
        const std::size_t close = line.find('}', open);
        if (open == std::string::npos || close == std::string::npos) {
            continue;
        }
        return toUpper(line.substr(open, close - open + 1));
    }
    return {};
}

// Cita `HKR,Events\<name>,Icon,,"%13%\G2710.Wia.dll,<broj>"` iz INF-a.
// Vraca broj onako kako je zapisan, sa znakom.
int infIconReference(const std::string& eventName) {
    std::ifstream file(G2710_INF_PATH);
    if (!file) {
        return 0;
    }
    const std::string needle = "Events\\" + eventName + ",Icon";
    std::string line;
    while (std::getline(file, line)) {
        if (line.find(needle) == std::string::npos) {
            continue;
        }
        const std::size_t comma = line.rfind(',');
        const std::size_t quote = line.find('"', comma);
        if (comma == std::string::npos || quote == std::string::npos) {
            continue;
        }
        return std::atoi(line.substr(comma + 1, quote - comma - 1).c_str());
    }
    return 0;
}

// Cita `<broj> ICON "<fajl>"` iz resursne skripte.
std::set<int> resourceScriptIconIds() {
    std::set<int> ids;
    std::ifstream file(G2710_WIA_RC_PATH);
    if (!file) {
        return ids;
    }
    std::string line;
    while (std::getline(file, line)) {
        if (line.empty() || line[0] == '/' || line.find(" ICON ") == std::string::npos) {
            continue;
        }
        ids.insert(std::atoi(line.c_str()));
    }
    return ids;
}

}  // namespace

// Ikone: INF, resursna skripta i Windows-ov nacin trazenja moraju se slagati.
//
// Ovo je nastalo iz stvarnog kvara. INF je pokazivao na "G2710.Wia.dll,101", a
// za Windows je nenegativan broj iza zareza REDNI BROJ ikone, ne njen ID.
// Drajver sa tri ikone nema ikonu broj 101, pa je kartica "Dogadjaji" na
// osobinama uredjaja crtala prazno mesto. U DLL-u tada ionako nije bilo
// nijedne ikone - resursne skripte uopste nije bilo.
//
// Ni jedno ni drugo nije prijavilo gresku: INF se overava, drajver se
// instalira, sve radi. Samo je slika prazna.
TEST(WiaEvents, IconsAreReferencedByResourceIdNotByIndex) {
    for (const char* name : {"Scan", "Copy", "Pdf"}) {
        const int reference = infIconReference(name);
        EXPECT_LT(reference, 0)
            << name << ": INF trazi ikonu rednim brojem " << reference
            << "; ID resursa se pise negativnim brojem";
    }
}

TEST(WiaEvents, EveryIconTheInfAsksForExistsInTheResourceScript) {
    const std::set<int> ids = resourceScriptIconIds();
    ASSERT_FALSE(ids.empty()) << "resursna skripta nema nijednu ikonu";

    for (const char* name : {"Scan", "Copy", "Pdf"}) {
        const int reference = infIconReference(name);
        ASSERT_NE(reference, 0) << name << ": INF nema Icon liniju";
        EXPECT_EQ(ids.count(std::abs(reference)), 1u)
            << name << ": INF trazi ID " << std::abs(reference)
            << ", a G2710Wia.rc ga ne definise";
    }
}

TEST(WiaEvents, EachButtonHasItsOwnIcon) {
    std::set<int> seen;
    for (const char* name : {"Scan", "Copy", "Pdf"}) {
        EXPECT_TRUE(seen.insert(infIconReference(name)).second)
            << name << ": dva dugmeta dele istu ikonu";
    }
}

// Broj dugmadi u kodu i u profilu mora biti isti. Ako se razidju, jedno od dva
// je zastarelo - a profil je generisan, pa je to kod.
TEST(WiaEvents, CountMatchesTheProfile) {
    EXPECT_EQ(static_cast<int>(buttonEvents().size()), buttonCountFromProfile());
}

TEST(WiaEvents, MasksAreTheOnesFromTheProfile) {
    const auto events = buttonEvents();
    ASSERT_EQ(events.size(), 3u);
    EXPECT_EQ(events[0].mask, 0x01u);
    EXPECT_EQ(events[1].mask, 0x02u);
    EXPECT_EQ(events[2].mask, 0x04u);
}

TEST(WiaEvents, EveryButtonHasItsOwnGuid) {
    std::set<std::string> seen;
    for (const auto& event : buttonEvents()) {
        ASSERT_NE(event.eventGuid, nullptr) << event.infName;
        EXPECT_TRUE(seen.insert(formatGuid(*event.eventGuid)).second)
            << "dva dugmeta dele isti GUID: " << event.infName;
    }
}

// Ovo je sustina fajla: kod i INF, znak po znak.
TEST(WiaEvents, GuidsMatchTheInf) {
    const struct {
        const wchar_t* infName;
        const char* stringsKey;
    } expected[] = {
        {L"Scan", "G2710.ScanEventGuid"},
        {L"Copy", "G2710.CopyEventGuid"},
        {L"Pdf", "G2710.PdfEventGuid"},
    };

    for (const auto& entry : expected) {
        const ButtonEvent* event = nullptr;
        for (const auto& candidate : buttonEvents()) {
            if (std::wstring(candidate.infName) == entry.infName) {
                event = &candidate;
            }
        }
        ASSERT_NE(event, nullptr) << "kod ne poznaje dugme iz INF-a";

        const std::string fromInf = infGuid(entry.stringsKey);
        ASSERT_FALSE(fromInf.empty()) << entry.stringsKey << " nije u INF-u";
        EXPECT_EQ(formatGuid(*event->eventGuid), fromInf) << entry.stringsKey;
    }
}

// Podrazumevana vrednost Events podkljuca MORA biti GUID. Prvo izdanje INF-a
// je tu imalo prikazno ime, sto bi dugmad ostavilo bez veze sa rukovaocem.
TEST(WiaEvents, InfEventSubkeysCarryGuidsNotDisplayNames) {
    std::ifstream file(G2710_INF_PATH);
    ASSERT_TRUE(file.is_open());

    int checked = 0;
    std::string line;
    while (std::getline(file, line)) {
        // Podrazumevana vrednost ima cetiri zareza pre vrednosti i nema ime.
        if (line.rfind("HKR,Events\\", 0) != 0 || line.find(",,,") == std::string::npos) {
            continue;
        }
        ++checked;
        EXPECT_NE(line.find("EventGuid%"), std::string::npos)
            << "podrazumevana vrednost nije GUID: " << line;
    }
    EXPECT_EQ(checked, 3) << "ocekuju se tri Events podkljuca";
}

// --- prevod maske ------------------------------------------------------------

TEST(WiaEvents, MaskMapsToTheRightEvent) {
    for (const auto& event : buttonEvents()) {
        const ButtonEvent* found = eventForMask(event.mask);
        ASSERT_NE(found, nullptr) << event.mask;
        EXPECT_EQ(found->eventGuid, event.eventGuid);
    }
}

TEST(WiaEvents, UnknownMaskHasNoEvent) {
    EXPECT_EQ(eventForMask(0x00u), nullptr) << "nula nije dogadjaj";
    EXPECT_EQ(eventForMask(0x08u), nullptr);
    EXPECT_EQ(eventForMask(0xF0u), nullptr);
}

// Uredjaj sme prijaviti vise dugmadi odjednom; STI nosi tacno jedan GUID po
// obavestenju, pa se uzima prvo poznato.
TEST(WiaEvents, SeveralButtonsAtOnceResolveToTheFirstKnownOne) {
    const ButtonEvent* both = eventForMask(0x01u | 0x02u);
    ASSERT_NE(both, nullptr);
    EXPECT_EQ(std::wstring(both->infName), L"Scan");

    const ButtonEvent* withNoise = eventForMask(0x80u | 0x04u);
    ASSERT_NE(withNoise, nullptr);
    EXPECT_EQ(std::wstring(withNoise->infName), L"Pdf")
        << "nepoznati bitovi se preskacu, poznati se prepoznaje";
}
