#include "CalibrationCache.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <system_error>

namespace g2710::calib {
namespace {

constexpr const char* kMagic = "G2710CAL";

const char* sectionTag(CalibrationSection section) noexcept {
    switch (section) {
        case CalibrationSection::Reflective:  return "flatbed";
        case CalibrationSection::Transparent: return "tma";
        case CalibrationSection::Negative:    return "neg";
    }
    return "?";
}

// Samo ono sto se sme naci u imenu fajla.
std::string sanitize(const std::string& text) {
    std::string clean;
    clean.reserve(text.size());
    for (char character : text) {
        const bool safe = (character >= 'a' && character <= 'z') ||
                          (character >= 'A' && character <= 'Z') ||
                          (character >= '0' && character <= '9') || character == '-' ||
                          character == '_';
        clean.push_back(safe ? character : '_');
    }
    return clean;
}

void writeDoubles(std::ostream& stream, const std::vector<double>& values) {
    const auto count = static_cast<std::uint64_t>(values.size());
    stream.write(reinterpret_cast<const char*>(&count), sizeof(count));
    if (count > 0) {
        stream.write(reinterpret_cast<const char*>(values.data()),
                     static_cast<std::streamsize>(count * sizeof(double)));
    }
}

bool readDoubles(std::istream& stream, std::vector<double>* values, std::uint64_t limit) {
    std::uint64_t count = 0;
    stream.read(reinterpret_cast<char*>(&count), sizeof(count));
    if (!stream || count > limit) {
        return false;
    }
    values->assign(static_cast<std::size_t>(count), 0.0);
    if (count > 0) {
        stream.read(reinterpret_cast<char*>(values->data()),
                    static_cast<std::streamsize>(count * sizeof(double)));
    }
    return static_cast<bool>(stream);
}

}  // namespace

bool CalibrationKey::operator==(const CalibrationKey& other) const noexcept {
    return deviceId == other.deviceId && resolution == other.resolution &&
           section == other.section && depth == other.depth;
}

std::string CalibrationKey::fileName() const {
    std::ostringstream name;
    name << sanitize(deviceId) << '-' << resolution << "dpi-" << sectionTag(section) << '-'
         << depth << "bit.cal";
    return name.str();
}

Result<std::string> CalibrationCache::defaultDirectory() {
    std::size_t length = 0;
    char* value = nullptr;
    if (_dupenv_s(&value, &length, "LOCALAPPDATA") != 0 || value == nullptr) {
        return fail(ErrorCode::Internal, "CalibrationCache: LOCALAPPDATA nije postavljen");
    }
    const std::string local(value);
    std::free(value);

    std::filesystem::path path(local);
    path /= "G2710";
    path /= "calibration";
    return path.string();
}

std::string CalibrationCache::pathFor(const CalibrationKey& key) const {
    return (std::filesystem::path(directory_) / key.fileName()).string();
}

bool CalibrationCache::contains(const CalibrationKey& key) const {
    if (!key.valid()) {
        return false;
    }
    std::error_code ignored;
    return std::filesystem::exists(pathFor(key), ignored);
}

Status CalibrationCache::store(const CalibrationRecord& record) const {
    if (!record.key.valid()) {
        return fail(ErrorCode::InvalidArgument, "CalibrationCache: neispravan kljuc");
    }
    if (record.shading.empty()) {
        return fail(ErrorCode::InvalidArgument, "CalibrationCache: prazni koeficijenti");
    }

    const std::size_t expected = record.shading.pixelsPerLine * kChannels;
    if (record.shading.darkOffset.size() != expected || record.shading.gain.size() != expected) {
        return fail(ErrorCode::InvalidArgument,
                    "CalibrationCache: duzina koeficijenata ne odgovara sirini");
    }

    std::error_code error;
    std::filesystem::create_directories(directory_, error);
    if (error) {
        return fail(ErrorCode::Internal, "CalibrationCache: ne mogu da napravim direktorijum");
    }

    // Prvo u privremeni fajl pa preimenovanje: prekid usred upisa ne sme
    // ostaviti pola zapisa koje ce sledeci put proci proveru.
    const std::string finalPath = pathFor(record.key);
    const std::string temporaryPath = finalPath + ".tmp";

    {
        std::ofstream stream(temporaryPath, std::ios::binary | std::ios::trunc);
        if (!stream) {
            return fail(ErrorCode::Internal, "CalibrationCache: ne mogu da otvorim fajl");
        }

        stream << kMagic << '\n'
               << kCalibrationCacheVersion << '\n'
               << record.key.deviceId << '\n'
               << record.key.resolution << '\n'
               << sectionTag(record.key.section) << '\n'
               << record.key.depth << '\n'
               << record.savedAtUnixSeconds << '\n'
               << record.shading.pixelsPerLine << '\n';

        writeDoubles(stream, record.shading.darkOffset);
        writeDoubles(stream, record.shading.gain);

        for (std::size_t channel = 0; channel < kChannels; ++channel) {
            stream.write(reinterpret_cast<const char*>(&record.gainOffset.evenDcg[channel]),
                         sizeof(int));
            stream.write(reinterpret_cast<const char*>(&record.gainOffset.oddDcg[channel]),
                         sizeof(int));
            stream.write(reinterpret_cast<const char*>(&record.gainOffset.gain[channel]),
                         sizeof(int));
        }

        if (!stream) {
            return fail(ErrorCode::Internal, "CalibrationCache: upis nije uspeo");
        }
    }

    std::filesystem::rename(temporaryPath, finalPath, error);
    if (error) {
        std::error_code ignored;
        std::filesystem::remove(temporaryPath, ignored);
        return fail(ErrorCode::Internal, "CalibrationCache: preimenovanje nije uspelo");
    }
    return ok();
}

Result<CalibrationRecord> CalibrationCache::load(const CalibrationKey& key) const {
    if (!key.valid()) {
        return fail(ErrorCode::InvalidArgument, "CalibrationCache: neispravan kljuc");
    }
    if (!contains(key)) {
        // Nekalibrisano nije otkaz - vrati prazan zapis.
        return CalibrationRecord{};
    }

    std::ifstream stream(pathFor(key), std::ios::binary);
    if (!stream) {
        return fail(ErrorCode::Internal, "CalibrationCache: ne mogu da otvorim fajl");
    }

    std::string magic;
    int version = 0;
    std::getline(stream, magic);
    stream >> version;
    stream.ignore();

    if (magic != kMagic) {
        return fail(ErrorCode::DeviceError, "CalibrationCache: ovo nije kalibracioni fajl");
    }
    if (version != kCalibrationCacheVersion) {
        return fail(ErrorCode::DeviceError, "CalibrationCache: druga verzija formata");
    }

    CalibrationRecord record;
    std::string sectionText;
    std::getline(stream, record.key.deviceId);
    stream >> record.key.resolution;
    stream.ignore();
    std::getline(stream, sectionText);
    stream >> record.key.depth >> record.savedAtUnixSeconds >> record.shading.pixelsPerLine;
    stream.ignore();

    if (!stream) {
        return fail(ErrorCode::DeviceError, "CalibrationCache: zaglavlje je nepotpuno");
    }

    record.key.section = sectionText == "tma"   ? CalibrationSection::Transparent
                         : sectionText == "neg" ? CalibrationSection::Negative
                                                : CalibrationSection::Reflective;

    if (!(record.key == key)) {
        return fail(ErrorCode::DeviceError, "CalibrationCache: zapis pripada drugom kljucu");
    }

    // Gornja granica postoji da ostecen brojac ne pokusa da alocira gigabajte.
    const std::uint64_t limit = 100u * 1000u * kChannels;
    if (!readDoubles(stream, &record.shading.darkOffset, limit) ||
        !readDoubles(stream, &record.shading.gain, limit)) {
        return fail(ErrorCode::DeviceError, "CalibrationCache: koeficijenti su nepotpuni");
    }

    const std::size_t expected = record.shading.pixelsPerLine * kChannels;
    if (record.shading.darkOffset.size() != expected || record.shading.gain.size() != expected) {
        return fail(ErrorCode::DeviceError,
                    "CalibrationCache: duzina koeficijenata ne odgovara sirini");
    }

    for (std::size_t channel = 0; channel < kChannels; ++channel) {
        stream.read(reinterpret_cast<char*>(&record.gainOffset.evenDcg[channel]), sizeof(int));
        stream.read(reinterpret_cast<char*>(&record.gainOffset.oddDcg[channel]), sizeof(int));
        stream.read(reinterpret_cast<char*>(&record.gainOffset.gain[channel]), sizeof(int));
    }
    if (!stream) {
        return fail(ErrorCode::DeviceError, "CalibrationCache: pojacanje i offset su nepotpuni");
    }

    return record;
}

Status CalibrationCache::remove(const CalibrationKey& key) const {
    if (!key.valid()) {
        return fail(ErrorCode::InvalidArgument, "CalibrationCache: neispravan kljuc");
    }
    std::error_code error;
    std::filesystem::remove(pathFor(key), error);
    if (error) {
        return fail(ErrorCode::Internal, "CalibrationCache: brisanje nije uspelo");
    }
    return ok();
}

Result<int> CalibrationCache::removeAllFor(const std::string& deviceId) const {
    if (deviceId.empty()) {
        return fail(ErrorCode::InvalidArgument, "CalibrationCache: prazna oznaka uredjaja");
    }

    std::error_code error;
    if (!std::filesystem::exists(directory_, error)) {
        return 0;
    }

    const std::string prefix = sanitize(deviceId) + "-";
    int removed = 0;

    for (const auto& entry : std::filesystem::directory_iterator(directory_, error)) {
        if (error) {
            return fail(ErrorCode::Internal, "CalibrationCache: ne mogu da procitam direktorijum");
        }
        const std::string name = entry.path().filename().string();
        if (name.rfind(prefix, 0) != 0 || entry.path().extension() != ".cal") {
            continue;
        }
        std::error_code removeError;
        std::filesystem::remove(entry.path(), removeError);
        if (!removeError) {
            ++removed;
        }
    }
    return removed;
}

}  // namespace g2710::calib
