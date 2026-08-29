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
#include <cstdio>
#include <chrono>
#include <string>
#include <utility>
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

    // Ubij proces DOK jos drzi bravu.
    //
    // Ovim nastaje pravi napusteni mutex - isto ono sto se desi kada klijent
    // pukne usred skeniranja. Ne moze se odglumiti unutar jednog procesa: dok
    // je vlasnik ziv, Windows bravu ne oglasava napustenom.
    void kill() {
        if (process_ != nullptr) {
            ::TerminateProcess(process_, 1);
            ::WaitForSingleObject(process_, 5000);
            ::CloseHandle(process_);
            process_ = nullptr;
        }
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

// Klijent koji pukne drzeci uredjaj ostavlja glavu bilo gde.
//
// Windows bravu tada dodeljuje sledecem, i to je ispravno - inace bi jedan pad
// zauvek zakljucao skener. Ali sledeci NE SME da nastavi kao da je uredjaj
// zatecen uredan: prolaz je mozda bio u toku, glava je mogla ostati na sredini
// stakla, lampa upaljena.
//
// Kod je to znao i pisalo je u komentaru ("sloj iznad mora izvrsiti HOME"), ali
// se nije prijavljivalo navise: WAIT_ABANDONED i WAIT_OBJECT_0 vodili su u isti
// `break`, pa sloj iznad nije imao odakle da sazna.
TEST(ArbiterCrossProcess, ADeadOwnerIsReportedToTheNextClient) {
    const std::string key = uniqueKey("abandoned");
    ReadyEvent ready{key + "-ready"};
    ASSERT_TRUE(ready.valid());

    // REDOSLED JE DEO POJAVE, ne udobnost testa.
    //
    // initialize() ide PRE nego sto drugi proces umre. Imenovani objekat zivi
    // dok ga bar neko drzi otvorenim; ako umre jedini vlasnik, objekat nestaje
    // i sledeci ga pravi iznova - nema sta da bude napusteno, pa nema ni
    // WAIT_ABANDONED. Prvi pokusaj ovog testa je ubijao pomocni proces pre
    // initialize() i padao je bas zato.
    //
    // To je i stvarno stanje kod korisnika: WIA servis i aplikacija oba drze
    // svoje handle-ove dok rade, pa pad jednog drugi VIDI. Sto ujedno znaci i
    // granicu koju priznajemo - vidi DeviceArbiter.h.
    DeviceArbiter arbiter{key};
    ASSERT_TRUE(arbiter.initialize());

    Holder holder;
    ASSERT_TRUE(holder.start(holdCommand(key, "WIA", 30000, ready.name())));
    ASSERT_TRUE(ready.wait(10s)) << "pomocni proces nije uzeo bravu";

    holder.kill();

    auto session = arbiter.acquireData(2s, "posle-pada");
    ASSERT_TRUE(session) << "brava mora biti dodeljena; jedan pad ne sme zakljucati skener";
    EXPECT_TRUE(session.value().held());
    EXPECT_TRUE(session.value().previousOwnerDied())
        << "sledeci klijent ne zna da je prethodni pao, pa ce racunati poziciju od nule";

    // Premestanje mora poneti i zastavicu.
    //
    // Ovo je pao dok je pisan: premestajuci konstruktor je kopirao dva stara
    // polja i tiho ispustao trece. Arbitraza je videla WAIT_ABANDONED - mereno,
    // 0x80 - a pozivalac je dobijao sesiju koja tvrdi da je sve bilo uredno.
    // Uredjaj se premesta kroz najmanje jedan move (Result -> pozivalac), pa je
    // put od otkrica do upotrebe isao bas tuda.
    DataSession moved = std::move(session).value();
    EXPECT_TRUE(moved.held());
    EXPECT_TRUE(moved.previousOwnerDied()) << "zastavica se izgubila u move-u";
}

// Uredan prolaz NE sme izgledati kao pad.
//
// Bez ovoga bi zastavica mogla biti postavljena uvek, test iznad bi prolazio, a
// svako skeniranje bi nepotrebno trazilo HOME.
TEST(ArbiterCrossProcess, AnOrderlyHandoverIsNotReportedAsADeath) {
    const std::string key = uniqueKey("orderly");
    ReadyEvent ready{key + "-ready"};
    ASSERT_TRUE(ready.valid());

    DeviceArbiter arbiter{key};
    ASSERT_TRUE(arbiter.initialize());

    Holder holder;
    ASSERT_TRUE(holder.start(holdCommand(key, "WIA", 300, ready.name())));
    ASSERT_TRUE(ready.wait(10s));
    ASSERT_EQ(kHolderOk, holder.wait(10s)) << "pomocni proces nije zavrsio uredno";

    auto session = arbiter.acquireData(2s, "posle-urednog");
    ASSERT_TRUE(session);
    EXPECT_FALSE(session.value().previousOwnerDied());
}

// Prvi klijent uopste nema prethodnika.
TEST(ArbiterCrossProcess, TheFirstClientHasNoPredecessor) {
    const std::string key = uniqueKey("prvi");
    DeviceArbiter arbiter{key};
    ASSERT_TRUE(arbiter.initialize());

    auto session = arbiter.acquireData(2s, "prvi");
    ASSERT_TRUE(session);
    EXPECT_FALSE(session.value().previousOwnerDied());
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
