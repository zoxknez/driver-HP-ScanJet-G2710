#include "CalibrationConfig.h"

#include <span>

namespace g2710::calib {
namespace {

using profile::CalibEntry;
using profile::CalibOption;

// Referenca skalira deo polja sa 0.01 pri ucitavanju (rts8822.c:11517-11528).
constexpr double kPercentScale = 0.01;

std::span<const CalibEntry> tableFor(CalibrationSection section) noexcept {
    switch (section) {
        case CalibrationSection::Reflective:
            return {profile::kCalibReflective,
                    sizeof(profile::kCalibReflective) / sizeof(CalibEntry)};
        case CalibrationSection::Transparent:
            return {profile::kCalibTransparent,
                    sizeof(profile::kCalibTransparent) / sizeof(CalibEntry)};
        case CalibrationSection::Negative:
            return {profile::kCalibNegative,
                    sizeof(profile::kCalibNegative) / sizeof(CalibEntry)};
    }
    return {};
}

// Opcije po kanalu su u enumu susedne: OFFSETEVEN1R, ...1G, ...1B.
CalibOption channelOption(CalibOption base, std::size_t channel) noexcept {
    return static_cast<CalibOption>(static_cast<int>(base) + static_cast<int>(channel));
}

}  // namespace

const char* toString(CalibrationSection section) noexcept {
    switch (section) {
        case CalibrationSection::Reflective:  return "reflective";
        case CalibrationSection::Transparent: return "transparent";
        case CalibrationSection::Negative:    return "negative";
    }
    return "?";
}

int calibrationValue(CalibrationSection section, CalibOption option,
                     int fallback) noexcept {
    for (const CalibEntry& entry : tableFor(section)) {
        if (entry.option == option) {
            return entry.value;
        }
    }
    return fallback;
}

Result<CalibrationConfig> loadCalibrationConfig(CalibrationSection section) {
    if (section != CalibrationSection::Reflective) {
        // TMA parametri POSTOJE u profilu, ali nisu kvalifikovani i nose dva
        // poznata defekta (docs/REFERENCE-DEFECTS.md). Tiho vracanje flatbed
        // vrednosti bilo bi gore od odbijanja.
        return fail(ErrorCode::NotImplementedIn10, "kalibracija: TMA nije u obimu 1.0");
    }

    const auto value = [section](CalibOption option, int fallback) {
        return calibrationValue(section, option, fallback);
    };

    CalibrationConfig config;

    config.whiteStripX = value(CalibOption::WSTRIPXPOS, 0);
    config.whiteStripY = value(CalibOption::WSTRIPYPOS, 0);
    config.blackStripX = value(CalibOption::BSTRIPXPOS, 0);

    // NAPOMENA: rts8822.c:11475 ovde cita WSTRIPYPOS umesto BSTRIPYPOS, sto
    // izgleda kao previd u referenci. Za G2710 su obe vrednosti nula pa
    // razlike nema; citamo BSTRIPYPOS jer je to ocigledna namera.
    config.blackStripY = value(CalibOption::BSTRIPYPOS, 0);

    for (std::size_t channel = 0; channel < kChannels; ++channel) {
        config.blackReference[channel] =
            value(channelOption(CalibOption::BREFR, channel), 10);
        config.offsetEven1[channel] =
            value(channelOption(CalibOption::OFFSETEVEN1R, channel), 256);
        config.offsetEven2[channel] =
            value(channelOption(CalibOption::OFFSETEVEN2R, channel), 0);
        config.offsetOdd1[channel] =
            value(channelOption(CalibOption::OFFSETODD1R, channel), 256);
        config.offsetOdd2[channel] =
            value(channelOption(CalibOption::OFFSETODD2R, channel), 0);
    }

    // Bele reference za flatbed NE dolaze iz ove tabele nego su fiksne u
    // hp3800_wrefs (248 / 250 / 248) - tabela wrefs vazi samo za TMA i negativ.
    // Vidi docs/G2710-PROFILE.md.
    for (std::size_t channel = 0; channel < kChannels; ++channel) {
        config.whiteReference[channel] = profile::kWhiteRefReflective[channel];
    }

    config.referenceBitDepth = value(CalibOption::REFBITDEPTH, 8);
    config.offsetHeight = value(CalibOption::OFFSETHEIGHT, 10);
    config.offsetNSigma = value(CalibOption::OFFSETNSIGMA, 2);

    config.offsetTargetMax = value(CalibOption::OFFSETTARGETMAX, 0x32) * kPercentScale;
    config.offsetTargetMin = value(CalibOption::OFFSETTARGETMIN, 2) * kPercentScale;
    config.offsetBoundaryRatio1 =
        value(CalibOption::OFFSETBOUNDARYRATIO1, 0x64) * kPercentScale;
    config.offsetBoundaryRatio2 =
        value(CalibOption::OFFSETBOUNDARYRATIO2, 0x64) * kPercentScale;
    config.offsetAvgRatio1 = value(CalibOption::OFFSETAVGRATIO1, 0x64) * kPercentScale;
    config.offsetAvgRatio2 = value(CalibOption::OFFSETAVGRATIO2, 0x64) * kPercentScale;

    config.gainHeight = value(CalibOption::GAINHEIGHT, 10);
    config.gainTargetFactor = value(CalibOption::GAINTARGETFACTOR, 80);

    config.calibrateOffset1 = value(CalibOption::CALIBOFFSET1ON, 0) != 0;
    config.calibrateOffset2 = value(CalibOption::CALIBOFFSET2ON, 0) != 0;
    config.calibrateGain1 = value(CalibOption::CALIBGAIN1ON, 0) != 0;
    config.calibrateGain2 = value(CalibOption::CALIBGAIN2ON, 0) != 0;
    config.calibratePaGain = value(CalibOption::CALIBPAGON, 0) != 0;

    // BSHADINGON je -3 a WSHADINGON je 3 u profilu; referenca ih testira kao
    // "razlicito od nule", ne kao pozitivno.
    config.blackShadingEnabled = value(CalibOption::BSHADINGON, 0) != 0;
    config.whiteShadingEnabled = value(CalibOption::WSHADINGON, 0) != 0;

    config.blackShadingHeight = value(CalibOption::BSHADINGHEIGHT, 20);
    config.whiteShadingHeight = value(CalibOption::WSHADINGHEIGHT, 24);

    return config;
}

}  // namespace g2710::calib
