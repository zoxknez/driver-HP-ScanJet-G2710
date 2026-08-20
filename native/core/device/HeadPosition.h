// Pozicija glave, sa eksplicitnim pojmom "ne znam gde je".
//
// Ovo nije int. Razlika izmedju "glava je na koraku 0" i "ne znam gde je
// glava" je sustinska: prva dozvoljava kretanje, druga zahteva HOME pre bilo
// cega drugog. Broj koji tiho pocinje od nule posle gubitka veze je tacno ona
// greska koja pomera glavu u kraj staze.
//
// Vidi docs/SAFETY.md, 2.

#pragma once

#include <cstdint>

namespace g2710 {

class HeadPosition {
public:
    // Podrazumevano stanje je NEPOZNATO. Uredjaj koji je tek otvoren nije
    // nuzno na home poziciji - prethodni klijent je mogao pasti u sred scana.
    HeadPosition() = default;

    bool isKnown() const noexcept { return known_; }

    // Vrednost ima smisla samo kada je isKnown(). Pozivalac koji to ne
    // proveri dobija 0, ali to nije "na home" nego "nemam podatak".
    int steps() const noexcept { return known_ ? steps_ : 0; }

    // Postavlja poziciju kao poznatu. Zove se posle uspesnog HOME-a ili posle
    // kretanja koje je zavrseno onako kako je planirano.
    void setKnown(int steps) noexcept {
        steps_ = steps;
        known_ = true;
    }

    // Poznata pozicija se pomera za `delta`. Ako pozicija NIJE poznata,
    // pomeranje je nema smisla i ostaje nepoznata - ne pravi se lazan podatak.
    void advance(int delta) noexcept {
        if (known_) {
            steps_ += delta;
        }
    }

    // Poziciju vise ne znamo. Zove se pri TransportLost, WAIT_ABANDONED, i
    // svaki put kada se kretanje zavrsi drugacije nego sto je planirano.
    void invalidate() noexcept {
        known_ = false;
        steps_ = 0;
        ++invalidations_;
    }

    // Koliko puta je pozicija izgubljena. Dijagnostika za H4/H13: broj veci
    // od nule u mirnom radu znaci da nesto prekida kretanje.
    int invalidationCount() const noexcept { return invalidations_; }

private:
    int steps_ = 0;
    bool known_ = false;
    int invalidations_ = 0;
};

}  // namespace g2710
