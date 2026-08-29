// Medjusobno iskljucivanje WIA / TWAIN / aplikacije / CLI nad jednim uredjajem.
//
// WIA servis radi u Session 0, a TWAIN, aplikacija i CLI u interaktivnoj
// sesiji. Named mutex bez eksplicitnog namespace-a je SESSION-LOCAL, pa bi
// svaka strana zakljucala SVOJ objekat i obe bi mislile da poseduju skener.
// Zato objekat ide u Global\ namespace sa eksplicitnim security descriptor-om
// koji dozvoljava i LocalSystem i interaktivnog korisnika.
//
//   StatusSession   jeftina, deljiva, read-only - NE blokira DataSession
//   DataSession     ekskluzivna, jedina sme da pomera motor
//
// Bez ovoga se problem javlja kao nasumican ERROR_SHARING_VIOLATION - kod
// prijatelja, na daljinu, bez debagera.

#pragma once

#include "../util/Result.h"

#include <chrono>
#include <string>

namespace g2710 {

// Da li je objekat stvarno medju-sesijski. Pad na Local\ je VIDLJIV, nikada
// tih - degradirano zakljucavanje je gore od nikakvog jer daje laznu sigurnost.
enum class ArbiterScope {
    Global,   // Global\ - ispravno, radi izmedju Session 0 i interaktivne
    Local,    // Local\  - DEGRADIRANO, ne stiti od WIA servisa
};

const char* toString(ArbiterScope scope) noexcept;

class DeviceArbiter;

// Ekskluzivan pristup. Samo drzalac ovoga sme izdavati motorne komande.
class DataSession {
public:
    DataSession() = default;
    ~DataSession();

    DataSession(DataSession&& other) noexcept;
    DataSession& operator=(DataSession&& other) noexcept;

    DataSession(const DataSession&) = delete;
    DataSession& operator=(const DataSession&) = delete;

    bool held() const noexcept { return mutex_ != nullptr; }
    void release() noexcept;

    /// Prethodni drzalac je UMRO ne pustivsi bravu.
    ///
    /// Windows bravu tada dodeljuje sledecem, ali uredjaj ostaje onako kako ga
    /// je mrtav proces ostavio: glava moze biti bilo gde, lampa upaljena,
    /// prolaz zapocet. Isto stanje kao posle TransportLost - pozicija je
    /// NEPOZNATA i HOME je obavezan pre bilo kakvog kretanja.
    ///
    /// Ranije se ovo nije prijavljivalo navise: WAIT_ABANDONED i WAIT_OBJECT_0
    /// vodili su u isti `break`, a komentar iznad njega je tvrdio da "sloj
    /// iznad mora izvrsiti HOME". Sloj iznad to nije imao kako da sazna.
    ///
    /// GRANICA KOJU PRIZNAJEMO, jer je bolje znati je nego se osloniti na nju:
    /// napustenost postoji samo dok objekat brave ZIVI. Ako umre jedini proces
    /// koji je drzi, imenovani objekat nestaje sa njim, a sledeci ga pravi
    /// iznova - i nema sta da bude napusteno. Ovo je dakle tacno kada je jos
    /// neko bio prikljucen u trenutku pada (WIA servis i aplikacija u isto
    /// vreme, sto je i najcesci slucaj), a ne kada se program pokrene TEK POSLE
    /// tudjeg pada. Za taj drugi slucaj jedini pouzdan odgovor je isti kao
    /// posle TransportLost: pozicija se ionako ne pamti izmedju pokretanja.
    bool previousOwnerDied() const noexcept { return previousOwnerDied_; }

private:
    friend class DeviceArbiter;
    DataSession(void* mutex, DeviceArbiter* owner, bool previousOwnerDied)
        : mutex_(mutex), owner_(owner), previousOwnerDied_(previousOwnerDied) {}

    void* mutex_ = nullptr;
    DeviceArbiter* owner_ = nullptr;
    bool previousOwnerDied_ = false;
};

// Deljiv, read-only pristup. Ne uzima bravu - status upit ne sme blokirati
// scan koji je vec u toku, niti obrnuto.
class StatusSession {
public:
    StatusSession() = default;
    bool valid() const noexcept { return true; }
};

class DeviceArbiter {
public:
    // `deviceKey` mora biti stabilan za dati uredjaj (npr. "03F0-2805").
    explicit DeviceArbiter(std::string deviceKey);
    ~DeviceArbiter();

    DeviceArbiter(const DeviceArbiter&) = delete;
    DeviceArbiter& operator=(const DeviceArbiter&) = delete;

    // Mora se pozvati pre acquire*(). Odvojeno od konstruktora da bi greska
    // pri kreiranju objekata bila obradiva, a ne izuzetak.
    Status initialize();

    // Domet BRAVE - ovo je korektnost arbitraze. Mora biti Global.
    ArbiterScope scope() const noexcept { return scope_; }
    const std::wstring& mutexName() const noexcept { return mutexName_; }

    // Domet DIJAGNOSTICKOG kanala sa imenom vlasnika. Odvojen od scope()
    // namerno, jer se ponasa drugacije:
    //
    //   CreateMutexW       sa Global\ USPEVA obicnom korisniku
    //   CreateFileMappingW sa Global\ PADA sa ERROR_ACCESS_DENIED
    //
    // SeCreateGlobalPrivilege se trazi za SECTION objekte, ne za mutekse, a
    // obican korisnik ga nema. WIA servis radi kao LocalSystem pa on kreira
    // Global blok, a interaktivni klijenti ga posle samo otvaraju.
    //
    // Ako kanal padne na Local, arbitraza je i dalje ISPRAVNA - gubi se samo
    // ime vlasnika u poruci. Zato ovo nikada ne obara scope().
    ArbiterScope ownerChannelScope() const noexcept { return ownerScope_; }
    bool ownerChannelAvailable() const noexcept { return ownerView_ != nullptr; }

    StatusSession acquireStatus() noexcept { return StatusSession{}; }

    // `clientName` se upisuje u deljeni blok da bi sledeci pozivalac, kada
    // istekne rok, dobio "uredjaj trenutno koristi WIA" umesto sirovog
    // Win32 koda.
    Result<DataSession> acquireData(std::chrono::milliseconds deadline,
                                    const char* clientName);

    // Ko trenutno drzi DataSession, ako se moze utvrditi.
    std::string currentOwner() const;

private:
    friend class DataSession;
    void onDataReleased() noexcept;

    std::string deviceKey_;
    std::wstring mutexName_;
    ArbiterScope scope_ = ArbiterScope::Global;
    ArbiterScope ownerScope_ = ArbiterScope::Global;

    void* mutex_ = nullptr;
    void* ownerMapping_ = nullptr;
    void* ownerView_ = nullptr;
};

}  // namespace g2710
