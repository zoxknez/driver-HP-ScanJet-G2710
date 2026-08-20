// CLSID u driver/g2710.inf i u native/wia/G2710Wia.h moraju biti IDENTICNI.
//
// Ako se raziidju, WIA servis ne moze da instancira minidriver, a greska se vidi
// tek kada je uredjaj prikljucen - kod prijatelja, na daljinu, kao "skener
// postoji ali ne radi". Zato je ovo test, a ne komentar.

#include "G2710Wia.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <string>

#ifndef G2710_INF_PATH
#error "G2710_INF_PATH mora biti definisan u build sistemu"
#endif

namespace {

std::string toUpper(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return text;
}

std::string headerClsid() {
    // GUID je cist ASCII, ali konverzija mora biti eksplicitna - implicitno
    // suzavanje wchar_t -> char je greska pod /W4 /WX, i s pravom.
    const std::wstring wide = G2710_WIA_CLSID_STRING;
    std::string narrow;
    narrow.reserve(wide.size());
    for (const wchar_t c : wide) {
        narrow.push_back(static_cast<char>(c & 0x7F));
    }
    return toUpper(narrow);
}

// Cita `G2710.CLSID = "{...}"` iz [Strings] sekcije INF-a.
std::string infClsid(std::string* diagnostic) {
    std::ifstream file(G2710_INF_PATH);
    if (!file) {
        *diagnostic = "INF se ne moze otvoriti: " G2710_INF_PATH;
        return {};
    }

    std::string line;
    while (std::getline(file, line)) {
        const std::size_t key = line.find("G2710.CLSID");
        if (key == std::string::npos || line.find(';') < key) {
            continue;
        }
        const std::size_t open = line.find('{', key);
        const std::size_t close = line.find('}', open);
        if (open == std::string::npos || close == std::string::npos) {
            continue;
        }
        return toUpper(line.substr(open, close - open + 1));
    }

    *diagnostic = "G2710.CLSID nije nadjen u [Strings] sekciji";
    return {};
}

}  // namespace

TEST(WiaClsid, HeaderAndInfAgree) {
    std::string diagnostic;
    const std::string fromInf = infClsid(&diagnostic);

    ASSERT_FALSE(fromInf.empty()) << diagnostic;
    EXPECT_EQ(fromInf, headerClsid())
        << "CLSID u INF-u i u G2710Wia.h se razlikuju; WIA servis nece moci "
           "da instancira minidriver";
}

TEST(WiaClsid, HeaderValueIsWellFormedGuid) {
    const std::string clsid = headerClsid();
    ASSERT_EQ(clsid.size(), 38u) << "GUID sa viticastim zagradama ima 38 znakova";
    EXPECT_EQ(clsid.front(), '{');
    EXPECT_EQ(clsid.back(), '}');

    // 8-4-4-4-12 sa crticama na 9, 14, 19, 24 (racunajuci pocetnu zagradu).
    for (const std::size_t dash : {9u, 14u, 19u, 24u}) {
        EXPECT_EQ(clsid[dash], '-') << "crtica nedostaje na poziciji " << dash;
    }
    for (std::size_t i = 1; i + 1 < clsid.size(); ++i) {
        if (clsid[i] == '-') {
            continue;
        }
        EXPECT_TRUE(std::isxdigit(static_cast<unsigned char>(clsid[i])))
            << "znak koji nije heksadecimalan na poziciji " << i;
    }
}
