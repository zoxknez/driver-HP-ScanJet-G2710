// Zajednicki identifikatori WIA minidriver-a.
//
// CLSID mora biti IDENTICAN vrednosti USDClass u driver/g2710.inf. Ako se
// razidju, WIA servis nece moci da instancira drajver, a greska se vidi tek
// kada je uredjaj prikljucen - kod prijatelja, na daljinu.
// tests/unit/wia_clsid_test.cpp zakljucava tu saglasnost.

#pragma once

#include <atomic>

// {2C4E8A1D-7F63-4B95-9E12-3A6D5C8B0417}
#define G2710_WIA_CLSID_STRING L"{2C4E8A1D-7F63-4B95-9E12-3A6D5C8B0417}"

#define G2710_WIA_FRIENDLY_NAME L"HP ScanJet G2710 WIA Driver"
