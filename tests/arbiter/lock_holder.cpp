// Drugi proces u testu arbitraze.
//
// Postoji zato sto se `Global\` ne moze dokazati unutar jednog procesa. Test u
// jednom procesu prolazi i kada je brava obican `std::mutex` - a bas ta razlika
// je razlog zasto DeviceArbiter uopste postoji.
//
// Upotreba:
//   lock_holder hold  <kljuc> <ime-klijenta> <ms-drzanja> <ime-dogadjaja>
//   lock_holder try   <kljuc> <ime-klijenta> <ms-roka>
//
// Izlazni kodovi su ugovor sa testom, ne poruke coveku:
//   0  uzeo je bravu (hold), odnosno uspeo da je uzme (try)
//   2  brava je zauzeta - drzi je neko drugi
//   3  initialize() nije uspeo
//   4  losi argumenti

#include "device/DeviceArbiter.h"

#include <windows.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>

using namespace g2710;

namespace {

constexpr int kOk = 0;
constexpr int kBusy = 2;
constexpr int kInitFailed = 3;
constexpr int kBadArguments = 4;

int usage() {
    std::fprintf(stderr,
                 "lock_holder hold <kljuc> <klijent> <ms> <dogadjaj>\n"
                 "lock_holder try  <kljuc> <klijent> <ms>\n");
    return kBadArguments;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 5) {
        return usage();
    }
    const std::string mode = argv[1];
    const std::string key = argv[2];
    const std::string client = argv[3];
    const auto milliseconds = std::chrono::milliseconds(std::atoi(argv[4]));

    DeviceArbiter arbiter{key};
    if (!arbiter.initialize()) {
        std::fprintf(stderr, "initialize nije uspeo\n");
        return kInitFailed;
    }

    if (mode == "try") {
        auto session = arbiter.acquireData(milliseconds, client.c_str());
        if (!session) {
            // Ime vlasnika ide na stdout da ga test moze procitati - to je i
            // poenta dijagnostickog kanala.
            std::printf("%s", arbiter.currentOwner().c_str());
            return kBusy;
        }
        return kOk;
    }

    if (mode != "hold" || argc < 6) {
        return usage();
    }

    auto session = arbiter.acquireData(milliseconds, client.c_str());
    if (!session) {
        return kBusy;
    }

    // Javi testu da je brava UZETA. Bez ovoga bi test morao da spava i nagadja,
    // a takav test pada na sporoj masini i niko ne zna zasto.
    const std::wstring eventName = [&] {
        const std::string narrow = argv[5];
        return std::wstring(narrow.begin(), narrow.end());
    }();

    if (HANDLE ready = ::OpenEventW(EVENT_MODIFY_STATE, FALSE, eventName.c_str());
        ready != nullptr) {
        ::SetEvent(ready);
        ::CloseHandle(ready);
    } else {
        std::fprintf(stderr, "dogadjaj %ls se ne moze otvoriti\n", eventName.c_str());
        return kBadArguments;
    }

    ::Sleep(2000);
    return kOk;
}
