// Trajno cuvanje kalibracije.
//
// Puna kalibracija trazi zagrevanje lampe, pomeranje glave na obe trake i vise
// prolaza - desetak sekundi u kojima se nista ne skenira. Ponavljati to pred
// svaki scan znaci da korisnik ceka bez razloga.
//
// Kljuc je uredjaj x rezolucija x izvor x dubina. Sve cetiri stavke menjaju
// koeficijente:
//
//   uredjaj      svaki primerak ima svoj senzor i svoju lampu
//   rezolucija   broj piksela po redu se menja, pa i duzina koeficijenata
//   izvor        flatbed i TMA imaju razlicite lampe i razlicite trake
//   dubina       bela referenca je izrazena na referentnoj dubini
//
// Format je NAMERNO samoopisan: tekstualno zaglavlje pa binarni podaci. Kada
// se sa prijateljevog racunara vrati kes koji ne radi, mora se moci videti
// CIME je snimljen, bez ijednog alata.

#pragma once

#include "../util/Result.h"
#include "AdcCalibration.h"
#include "CalibrationConfig.h"
#include "ShadingCalibration.h"

#include <cstdint>
#include <string>

namespace g2710::calib {

// Verzija formata. Podize se kad god se promeni ono sto se upisuje - stari
// fajl se tada ODBIJA, ne cita napola.
inline constexpr int kCalibrationCacheVersion = 1;

struct CalibrationKey {
    // Stabilna oznaka uredjaja. Serijski broj ako ga ima, inace nesto sto se
    // ne menja izmedju pokretanja.
    std::string deviceId;

    int resolution = 0;
    CalibrationSection section = CalibrationSection::Reflective;
    int depth = 8;

    bool valid() const noexcept {
        return !deviceId.empty() && resolution > 0 && (depth == 8 || depth == 16);
    }

    // Ime fajla bez direktorijuma. Deterministicko, bez znakova koje
    // datotecni sistem ne voli.
    std::string fileName() const;

    bool operator==(const CalibrationKey& other) const noexcept;
};

struct CalibrationRecord {
    CalibrationKey key;
    ShadingCoefficients shading;
    GainOffsetState gainOffset;

    // Sekunde od epohe u trenutku upisa. Nula znaci nepoznato.
    //
    // Ne uzima se iz sata unutar modula: pozivalac zna koje vreme je merodavno
    // i testovi tako ostaju deterministicki.
    std::int64_t savedAtUnixSeconds = 0;

    bool empty() const noexcept { return shading.empty(); }
};

// Kes na disku.
//
// Direktorijum se pravi pri prvom upisu. Citanje nepostojeceg zapisa NIJE
// greska - vraca prazan rezultat, jer je "jos nije kalibrisano" normalno
// stanje, ne otkaz.
class CalibrationCache {
public:
    explicit CalibrationCache(std::string directory) : directory_(std::move(directory)) {}

    const std::string& directory() const noexcept { return directory_; }

    // Podrazumevana putanja: %LOCALAPPDATA%\G2710\calibration.
    //
    // WIA radi u Session 0 pod LocalSystem, pa tamo pada u profil te naloga -
    // sto je ispravno: kalibracija jednog korisnika ne treba drugom.
    static Result<std::string> defaultDirectory();

    // Da li zapis postoji.
    bool contains(const CalibrationKey& key) const;

    // Ucitaj. Prazan rezultat znaci "nema zapisa"; greska znaci da zapis
    // postoji ali je neupotrebljiv.
    Result<CalibrationRecord> load(const CalibrationKey& key) const;

    Status store(const CalibrationRecord& record) const;

    // Obrisi jedan zapis. Brisanje nepostojeceg nije greska.
    Status remove(const CalibrationKey& key) const;

    // Obrisi sve zapise ovog uredjaja. Vraca koliko ih je obrisano.
    Result<int> removeAllFor(const std::string& deviceId) const;

private:
    std::string pathFor(const CalibrationKey& key) const;

    std::string directory_;
};

}  // namespace g2710::calib
