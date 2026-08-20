// Otkazivanje operacije u toku.
//
// Mora biti bezbedno postaviti iz DRUGOG thread-a dok operacija traje - to je
// jedini nacin da cancel usred scana uopste ima smisla. Zato atomic, a ne
// obican bool.

#pragma once

#include <atomic>
#include <memory>

namespace g2710 {

class CancellationToken {
public:
    CancellationToken() : flag_(std::make_shared<std::atomic<bool>>(false)) {}

    bool isCancelled() const noexcept {
        return flag_->load(std::memory_order_acquire);
    }

    // Zove se iz thread-a koji otkazuje.
    void cancel() noexcept { flag_->store(true, std::memory_order_release); }

    void reset() noexcept { flag_->store(false, std::memory_order_release); }

    // Token koji se nikada ne otkazuje. Postoji da pozivalac koji ne zeli
    // otkazivanje ne bi prosledio nullptr - nijedna motorna operacija ne sme
    // primiti prazan token.
    static CancellationToken never() { return CancellationToken{}; }

private:
    std::shared_ptr<std::atomic<bool>> flag_;
};

}  // namespace g2710
