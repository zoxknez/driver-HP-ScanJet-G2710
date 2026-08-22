// Arbitraza izmedju DVA PROCESA.
//
// Acceptance gate faze G2710-4. Postojeci unit/device_arbiter_test.cpp radi u
// jednom procesu, a bas to je ono sto ne dokazuje nista: test u jednom procesu
// prolazi i kada je brava obican `std::mutex`. `Global\` postoji zato sto
// proces nije granica koja nas zanima.
//
// STA OVAJ FAJL DOKAZUJE:
//
//   dva odvojena procesa otimaju se o isti Global\ objekat i tacno jedan
//   dobija DataSession; drugi dobija IME VLASNIKA, ne sirov Win32 kod.
//
// STA NE DOKAZUJE, i zasto:
//
//   pravi Session 0 <-> interaktivna sesija. Za to je potreban Windows servis
//   koji radi kao LocalSystem, a to se ne moze podici iz test binarnog fajla.
//   Ono sto Global\ i Local\ zaista razlikuje - granica sesije - ostaje H12.
//
//   Razlika koja se OVDE hvata je ipak stvarna: da je brava proces-lokalna
//   (std::mutex, ili Local\ bez deljenja imena), svaki od ova dva procesa bi
//   uzeo svoju i oba bi mislila da poseduju skener.

#include "device/DeviceArbiter.h"

#include <windows.h>

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <string>
#include <vector>

using namespace g2710;
using namespace std::chrono_literals;

namespace {

constexpr int kHolderOk = 0;
constexpr int kHolderBusy = 2;

// Putanja do pomocnog procesa. Stoji pored test binarnog fajla; CMake ga
// prosledjuje kao definiciju da se ne pogadja.
const char* holderPath() { return G2710_LOCK_HOLDER_PATH; }

std::string uniqueKey(const char* label) {
    static std::atomic<int> counter{0};
    return std::string("xproc-") + label + "-" +
           std::to_string(counter.fetch_add(1)) + "-" +
           std::to_string(::GetCurrentProcessId());
}

// Pokrenut pomocni proces, sa cevi za njegov stdout.
class Holder {
public:
    Holder() = default;

    ~Holder() {
        if (process_ != nullptr) {
            ::TerminateProcess(process_, 1);
            ::CloseHandle(process_);
        }
        if (readEnd_ != nullptr) {
            ::CloseHandle(readEnd_);
        }
    }

    Holder(const Holder&) = delete;
    Holder& operator=(const Holder&) = delete;

    bool start(const std::string& commandLine) {
        SECURITY_ATTRIBUTES inherit{};
        inherit.nLength = sizeof(inherit);
        inherit.bInheritHandle = TRUE;

        HANDLE writeEnd = nullptr;
        if (::CreatePipe(&readEnd_, &writeEnd, &inherit, 0) == 0) {
            return false;
        }
        // Nas kraj cevi se NE nasledjuje - inace se citanje nikad ne zavrsava,
        // jer dete drzi otvoren pisuci kraj kojim mi mislimo da smo jedini.
        ::SetHandleInformation(readEnd_, HANDLE_FLAG_INHERIT, 0);

        STARTUPINFOA startup{};
        startup.cb = sizeof(startup);
        startup.dwFlags = STARTF_USESTDHANDLES;
        startup.hStdOutput = writeEnd;
        startup.hStdError = ::GetStdHandle(STD_ERROR_HANDLE);
        startup.hStdInput = ::GetStdHandle(STD_INPUT_HANDLE);

        PROCESS_INFORMATION info{};
        std::vector<char> mutable_(commandLine.begin(), commandLine.end());
        mutable_.push_back('\0');

        const BOOL created = ::CreateProcessA(nullptr, mutable_.data(), nullptr, nullptr,
                                              TRUE, 0, nullptr, nullptr, &startup, &info);
        ::CloseHandle(writeEnd);
        if (created == 0) {
            ::CloseHandle(readEnd_);
            readEnd_ = nullptr;
            return false;
        }
        ::CloseHandle(info.hThread);
        process_ = info.hProcess;
        return true;
    }

    // Cekaj kraj i vrati izlazni kod; -1 ako je isteklo.
    int wait(std::chrono::milliseconds deadline) {
        if (process_ == nullptr) {
            return -1;
        }
        if (::WaitForSingleObject(process_, static_cast<DWORD>(deadline.count())) !=
            WAIT_OBJECT_0) {
            return -1;
        }
        DWORD code = 0;
        ::GetExitCodeProcess(process_, &code);
        return static_cast<int>(code);
    }

    std::string output() {
        std::string text;
        char buffer[256];
        DWORD read = 0;
        while (::ReadFile(readEnd_, buffer, sizeof(buffer), &read, nullptr) != 0 && read > 0) {
            text.append(buffer, read);
        }
        return text;
    }

private:
    HANDLE process_ = nullptr;
    HANDLE readEnd_ = nullptr;
};

// Dogadjaj kojim pomocni proces javlja da je bravu UZEO.
//
// Bez njega bi test spavao i nagadjao, a takav test pada na sporoj masini i
// niko ne zna zasto.
class ReadyEvent {
public:
    explicit ReadyEvent(const std::string& name) : name_(name) {
        const std::wstring wide(name.begin(), name.end());
        handle_ = ::CreateEventW(nullptr, TRUE, FALSE, wide.c_str());
    }
    ~ReadyEvent() {
        if (handle_ != nullptr) {
            ::CloseHandle(handle_);
        }
    }

    ReadyEvent(const ReadyEvent&) = delete;
    ReadyEvent& operator=(const ReadyEvent&) = delete;

    bool valid() const noexcept { return handle_ != nullptr; }
    const std::string& name() const noexcept { return name_; }

    bool wait(std::chrono::milliseconds deadline) const {
        return ::WaitForSingleObject(handle_, static_cast<DWORD>(deadline.count())) ==
               WAIT_OBJECT_0;
    }

private:
    std::string name_;
    HANDLE handle_ = nullptr;
};

std::string holdCommand(const std::string& key, const char* client, int holdMs,
                        const std::string& eventName) {
    return std::string("\"") + holderPath() + "\" hold " + key + " " + client + " " +
           std::to_string(holdMs) + " " + eventName;
}

std::string tryCommand(const std::string& key, const char* client, int deadlineMs) {
    return std::string("\"") + holderPath() + "\" try " + key + " " + client + " " +
           std::to_string(deadlineMs);
}

}  // namespace

// Ako pomocni proces ne postoji, svi testovi ispod bi "prosli" ne merivsi
// nista. Ovo pada glasno.
TEST(ArbiterCrossProcess, TheHelperProcessExists) {
    ASSERT_NE(INVALID_FILE_ATTRIBUTES, ::GetFileAttributesA(holderPath()))
        << "nema " << holderPath();
}

TEST(ArbiterCrossProcess, OnlyOneProcessGetsTheDataSession) {
    const std::string key = uniqueKey("exclusive");
    ReadyEvent ready{key + "-ready"};
    ASSERT_TRUE(ready.valid());

    Holder holder;
    ASSERT_TRUE(holder.start(holdCommand(key, "WIA", 5000, ready.name())));
    ASSERT_TRUE(ready.wait(10s)) << "pomocni proces nije uzeo bravu";

    // Od ovog trenutka brava je kod DRUGOG procesa. Da je proces-lokalna, ovo
    // bi uspelo - i oba procesa bi mislila da poseduju skener.
    DeviceArbiter arbiter{key};
    ASSERT_TRUE(arbiter.initialize());

    auto denied = arbiter.acquireData(200ms, "TWAIN");
    ASSERT_FALSE(denied) << "dva procesa su istovremeno dobila DataSession";
    EXPECT_EQ(ErrorCode::Busy, denied.error().code);

    EXPECT_EQ(kHolderOk, holder.wait(10s));
}

TEST(ArbiterCrossProcess, TheLockIsReleasedWhenTheOtherProcessExits) {
    const std::string key = uniqueKey("release");
    ReadyEvent ready{key + "-ready"};
    ASSERT_TRUE(ready.valid());

    Holder holder;
    ASSERT_TRUE(holder.start(holdCommand(key, "WIA", 5000, ready.name())));
    ASSERT_TRUE(ready.wait(10s));

    DeviceArbiter arbiter{key};
    ASSERT_TRUE(arbiter.initialize());
    ASSERT_FALSE(arbiter.acquireData(200ms, "TWAIN"));

    ASSERT_EQ(kHolderOk, holder.wait(10s));

    // Brava mora biti slobodna kada vlasnik ode. Ako ostane zauzeta, skener je
    // nedostupan do restarta - i to bi se videlo tek kod prijatelja.
    auto granted = arbiter.acquireData(5s, "TWAIN");
    EXPECT_TRUE(granted) << "brava nije oslobodjena posle izlaska vlasnika";
}

TEST(ArbiterCrossProcess, TheWaitingClientLearnsWhoHoldsTheDevice) {
    const std::string key = uniqueKey("owner");

    DeviceArbiter arbiter{key};
    ASSERT_TRUE(arbiter.initialize());

    auto held = arbiter.acquireData(2s, "WIA");
    ASSERT_TRUE(held);

    Holder holder;
    ASSERT_TRUE(holder.start(tryCommand(key, "TWAIN", 200)));
    ASSERT_EQ(kHolderBusy, holder.wait(10s));

    // Poenta dijagnostickog kanala: covek treba da vidi "uredjaj koristi WIA",
    // a ne ERROR_SHARING_VIOLATION.
    //
    // Kanal sme da padne na Local i tada ime izostaje - to arbitrazu ne kvari,
    // pa se ovde i ne trazi bezuslovno. Ali ako je kanal Global, ime MORA biti
    // tacno; prazno ime bi znacilo da se blok pise a ne cita.
    const std::string owner = holder.output();
    if (arbiter.ownerChannelScope() == ArbiterScope::Global &&
        arbiter.ownerChannelAvailable()) {
        EXPECT_NE(std::string::npos, owner.find("WIA"))
            << "kanal je Global ali ime vlasnika nije stiglo: '" << owner << "'";
    }
}

TEST(ArbiterCrossProcess, TwoProcessesWithDifferentDevicesDoNotBlockEachOther) {
    const std::string mine = uniqueKey("mine");
    const std::string theirs = uniqueKey("theirs");

    ReadyEvent ready{theirs + "-ready"};
    ASSERT_TRUE(ready.valid());

    Holder holder;
    ASSERT_TRUE(holder.start(holdCommand(theirs, "WIA", 5000, ready.name())));
    ASSERT_TRUE(ready.wait(10s));

    // Kljuc uredjaja je deo imena brave. Da nije, dva skenera na istom
    // racunaru bi se medjusobno blokirala.
    DeviceArbiter arbiter{mine};
    ASSERT_TRUE(arbiter.initialize());
    EXPECT_TRUE(arbiter.acquireData(200ms, "TWAIN"));

    EXPECT_EQ(kHolderOk, holder.wait(10s));
}

TEST(ArbiterCrossProcess, AStatusSessionDoesNotBlockAnotherProcess) {
    const std::string key = uniqueKey("status");

    DeviceArbiter arbiter{key};
    ASSERT_TRUE(arbiter.initialize());

    // Status upit je jeftin i deljiv. Da uzima bravu, WIA servis bi svojim
    // periodicnim proverama stanja obarao svaki scan u drugoj sesiji.
    StatusSession status = arbiter.acquireStatus();
    ASSERT_TRUE(status.valid());

    Holder holder;
    ASSERT_TRUE(holder.start(tryCommand(key, "TWAIN", 1000)));
    EXPECT_EQ(kHolderOk, holder.wait(10s))
        << "status sesija je blokirala podatke u drugom procesu";
}
