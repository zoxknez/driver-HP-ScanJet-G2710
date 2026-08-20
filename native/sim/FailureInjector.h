// Injekcija otkaza na nivou transporta.
//
// Postoji zbog jedne recenice iz docs/SAFETY.md: svaki otkaz mora voditi u
// definisano stanje, nikada u hang. To se ne moze tvrditi bez nacina da se
// otkaz izazove kad se hoce.
//
// Otkazi se ne dogadjaju nasumicno. Zakazuju se eksplicitno - "posle jos dve
// operacije, tri puta zaredom" - pa je test koji padne uvek ponovljiv.

#pragma once

#include "../core/util/Error.h"

#include <cstddef>
#include <optional>
#include <vector>

namespace g2710::sim {

// Nad kojom vrstom prenosa otkaz deluje.
enum class TransferKind {
    ControlIn,
    ControlOut,
    BulkRead,
    BulkWrite,
    Event,
    Any,
};

const char* toString(TransferKind kind) noexcept;

struct FaultSchedule {
    TransferKind kind = TransferKind::Any;
    ErrorCode error = ErrorCode::Timeout;

    // Koliko odgovarajucih operacija proci pre nego sto otkaz pocne.
    int afterOperations = 0;

    // Koliko puta otkaz da se ponovi. Negativno znaci zauvek - tako se
    // modelira iscupan kabl, koji se sam od sebe ne popravlja.
    int repeat = 1;
};

class FailureInjector {
public:
    void schedule(const FaultSchedule& fault) { schedule_.push_back(fault); }

    void injectOnce(TransferKind kind, ErrorCode error) {
        schedule(FaultSchedule{kind, error, 0, 1});
    }

    // Trajni otkaz - modelira iscupan USB, ne prolazni problem.
    void injectPermanent(TransferKind kind, ErrorCode error) {
        schedule(FaultSchedule{kind, error, 0, -1});
    }

    void clear() noexcept { schedule_.clear(); }
    bool empty() const noexcept { return schedule_.empty(); }

    // Zove se pri svakoj operaciji. Vraca kod greske ako je na redu otkaz.
    std::optional<ErrorCode> nextFault(TransferKind kind);

    // Koliko je otkaza do sada izdato. Test moze da tvrdi da se otkaz stvarno
    // dogodio, umesto da prolazi zato sto se nikada nije desio.
    int firedCount() const noexcept { return fired_; }

private:
    static bool matches(TransferKind scheduled, TransferKind actual) noexcept;

    std::vector<FaultSchedule> schedule_;
    int fired_ = 0;
};

}  // namespace g2710::sim
