// Virtuelni motor sa mehanickim granicama.
//
// Postoji da bi se MotionGuard iz G2710-4 uopste mogao testirati. Motor koji
// se samo "pomeri koliko mu se kaze" ne bi razlikovao ispravan kod od koda
// koji zabija glavu u kraj staze - a to je jedina greska u ovom projektu koja
// moze fizicki ostetiti tudji uredjaj.
//
// Pozicija je u koracima na rezoluciji motora (1200 dpi za G2710), merena od
// home pozicije.

#pragma once

#include <cstdint>

namespace g2710::sim {

enum class MotorDirection {
    Forward,   // od home ka kraju staze
    Backward,  // ka home
};

// Sta se desilo pri pokusaju kretanja.
enum class MotorOutcome {
    Moved,        // presao trazeni broj koraka
    HitHome,      // stao na home senzoru
    HitFarLimit,  // stao na kraju staze - u stvarnosti bi ovo bio udarac
    Stalled,      // injektovan zastoj
    Disabled,     // motor nije ukljucen
};

const char* toString(MotorOutcome outcome) noexcept;

struct MotorGeometry {
    // Rezolucija motora u dpi. G2710 ima 1200, dok je senzor 2400.
    int resolution = 1200;

    // Duzina staze u milimetrima. Flatbed je 300 mm (cfg_constrains_get).
    int travelMillimetres = 300;

    // Koliko koraka pre home pozicije senzor jos uvek prijavljuje "kod kuce".
    // Pravi senzor nije tacka nego prozor.
    int homeWindowSteps = 8;

    // Ukupan broj koraka od home do kraja staze.
    constexpr int maxPositionSteps() const noexcept {
        // mm -> inch -> koraci
        return static_cast<int>((static_cast<double>(travelMillimetres) / 25.4) *
                                static_cast<double>(resolution));
    }
};

class VirtualMotor {
public:
    explicit VirtualMotor(MotorGeometry geometry = {}) : geometry_(geometry) {}

    const MotorGeometry& geometry() const noexcept { return geometry_; }

    int position() const noexcept { return position_; }
    bool isAtHome() const noexcept { return position_ <= geometry_.homeWindowSteps; }
    bool isAtFarLimit() const noexcept { return position_ >= geometry_.maxPositionSteps(); }

    bool enabled() const noexcept { return enabled_; }
    void setEnabled(bool enabled) noexcept { enabled_ = enabled; }

    MotorDirection direction() const noexcept { return direction_; }
    void setDirection(MotorDirection direction) noexcept { direction_ = direction; }

    // Ukupan broj koraka od pocetka simulacije. Test moze da tvrdi da
    // operacija koja je trebalo da bude jeftina nije prosla pola staze.
    long long totalSteps() const noexcept { return totalSteps_; }

    // Koliko puta je motor udario u kraj staze. Ne resetuje se sa position();
    // ako je ovo vece od nule, kod je negde pokusao da ide predaleko.
    int farLimitHits() const noexcept { return farLimitHits_; }

    // Pomeri `steps` koraka u trenutnom smeru. Zaustavlja se na granicama i to
    // prijavljuje - NE prelazi ih tiho.
    MotorOutcome step(int steps) noexcept;

    // Postavi poziciju bez kretanja. Za pripremu testa, ne za simulaciju.
    void teleportTo(int position) noexcept;

    // --- injekcija greske ----------------------------------------------

    // Zaustavi motor posle `afterSteps` koraka. Modelira mehanicki zastoj:
    // komanda je prihvacena, ali se glava nije pomerila do kraja.
    void injectStall(int afterSteps) noexcept { stallAfter_ = afterSteps; }
    void clearStall() noexcept { stallAfter_ = -1; }

private:
    MotorGeometry geometry_;
    int position_ = 0;
    bool enabled_ = true;
    MotorDirection direction_ = MotorDirection::Forward;
    long long totalSteps_ = 0;
    int farLimitHits_ = 0;
    int stallAfter_ = -1;
};

}  // namespace g2710::sim
