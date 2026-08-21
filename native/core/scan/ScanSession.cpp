#include "ScanSession.h"

#include "G2710Profile.generated.h"
#include "../image/LineOffset.h"

#include <algorithm>
#include <cstring>

namespace g2710::scan {
namespace {

// Bulk stize u komadima; ovoliki je dovoljan da red retko bude podeljen.
constexpr std::size_t kChunkBytes = 64 * 1024;

}  // namespace

ScanSession::ScanSession(rts8822::RegisterFile& registers, SafetyGate gate,
                         const ScanPlan& plan, ScanOptions options)
    : registers_(registers),
      gate_(gate),
      scanRegisters_(registers, gate),
      chip_(registers.transport(), gate),  // RegisterFile je bez stanja, pa druga kopija ne smeta
      plan_(plan),
      options_(std::move(options)) {
    lineart_ = plan_.requestedRegion.width > 0 &&
               plan_.outputLine.depthCode == image::DepthCode::Lineart;

    nativeWidth_ = static_cast<std::size_t>(std::max(0, plan_.nativeRegion.width));
    outputWidth_ = static_cast<std::size_t>(std::max(0, plan_.requestedRegion.width));

    channelsIn_ = image::channelsPerDot(plan_.tableMode);
    channelsOut_ = image::channelsPerDot(
        lineart_ ? image::ColorMode::Lineart
                 : (plan_.tableMode == image::ColorMode::Color && !lineart_
                        ? image::ColorMode::Color
                        : image::ColorMode::Gray));

    // Sivo i lineart citaju jedan kanal iz tabele, ali boja se pretvara u sivo
    // tek ovde ako je trazeno sivo a red je u boji. Za G2710 sivi red postoji,
    // pa je ulaz vec jednokanalni.
    wideChannel_ = plan_.hardwareDepth == 16;
    hardwareLineBytes_ = plan_.hardwareLine.bytesPerLine;

    rawLine_.assign(hardwareLineBytes_, 0);
    chunk_.assign(kChunkBytes, std::byte{0});

    planes_.assign(static_cast<std::size_t>(channelsIn_),
                   std::vector<std::uint16_t>(nativeWidth_, 0));

    if (!plan_.useHardwareAlignment && plan_.tableMode == image::ColorMode::Color &&
        plan_.softwareLineDistance > 0) {
        corrector_ = std::make_unique<image::LineOffsetCorrector>(nativeWidth_,
                                                                 plan_.softwareLineDistance);
        alignedLine_.assign(nativeWidth_ * image::kChannels, 0);
    }

    shadingApplied_ = !options_.shading.empty() &&
                      options_.shading.pixelsPerLine == nativeWidth_;

    if (lineart_) {
        nativeBits_.assign((nativeWidth_ + 7) / 8, 0);
        resizedBits_.assign((outputWidth_ + 7) / 8, 0);
    } else {
        nativeValues_.assign(nativeWidth_ * static_cast<std::size_t>(channelsOut_), 0);
        resizedValues_.assign(outputWidth_ * static_cast<std::size_t>(channelsOut_), 0);
    }

    if (plan_.resize == ResizeType::Decrease) {
        if (lineart_) {
            lineartResampler_ = std::make_unique<image::VerticalLineartResampler>(
                plan_.nativeResolution, plan_.requestedResolution, outputWidth_);
        } else {
            resampler_ = std::make_unique<image::VerticalResampler>(
                plan_.nativeResolution, plan_.requestedResolution, resizedValues_.size());
        }
    }

    expectedLines_ = std::max(0, plan_.requestedRegion.height);
}

ScanSession::~ScanSession() {
    // Mreza za sigurnost: ni izuzetak ni rani return ne smeju ostaviti
    // uredjaj koji skenira. Greska se ovde ne moze prijaviti, pa se broji.
    if (started_ && !finished_) {
        if (!scanRegisters_.warmReset()) {
            ++stats_.unclosedPasses;
        }
        finished_ = true;
    }
}

Status ScanSession::finish() {
    if (!started_ || finished_) {
        return ok();
    }
    finished_ = true;
    return scanRegisters_.warmReset();
}

std::size_t ScanSession::outputBytesPerLine() const noexcept {
    return plan_.outputLine.bytesPerLine;
}

Status ScanSession::begin() {
    if (started_) {
        return fail(ErrorCode::InvalidState, "ScanSession::begin: vec pokrenuto");
    }
    if (nativeWidth_ == 0 || outputWidth_ == 0 || hardwareLineBytes_ == 0) {
        return fail(ErrorCode::InvalidArgument, "ScanSession::begin: prazna geometrija");
    }

    // Lampa mora goreti PRE nego sto se prolaz pokrene.
    auto lampStatus = chip_.lampStatus();
    if (!lampStatus) {
        return lampStatus.error();
    }
    if (!lampStatus.value().flatbedOn) {
        return fail(ErrorCode::InvalidState,
                    "ScanSession::begin: flatbed lampa je ugasena");
    }

    const int ratio = profile::kSensor.resolution / plan_.nativeResolution;
    if (ratio < 1) {
        return fail(ErrorCode::InvalidArgument, "ScanSession::begin: neispravan odnos rezolucija");
    }
    if (const Status s = scanRegisters_.setResolutionRatio(ratio); !s) {
        return s;
    }

    // Jedan red slike po jednom dummy redu. Referenca ovde ume da stavi vise
    // za spore rezolucije; to pripada portu RTS_Setup_Motor i dok ne stigne,
    // drzi se jedan - vrednost koja je tacna i proverljiva.
    constexpr int kDummyLine = 1;
    if (const Status s = scanRegisters_.setDummyLine(kDummyLine); !s) {
        return s;
    }

    if (plan_.useHardwareAlignment) {
        if (const Status s = scanRegisters_.setLineOffsets(plan_.lineOffsets); !s) {
            return s;
        }
    } else {
        if (const Status s = scanRegisters_.clearLineOffsets(); !s) {
            return s;
        }
    }

    rts8822::CoordinateScaling scaling;
    scaling.resolutionRatio = ratio;
    scaling.dummyLine = kDummyLine;
    scaling.lineOffsetPadding =
        plan_.useHardwareAlignment ? plan_.lineOffsets.doublePlusEvenOdd : 0;
    scaling.softwareLineDistance = plan_.softwareLineDistance;

    rts8822::ScanGeometry pixels;
    pixels.left = plan_.nativeRegion.left;
    pixels.top = plan_.nativeRegion.top;
    pixels.width = plan_.nativeRegion.width;
    pixels.height = plan_.nativeRegion.height;

    if (const Status s = scanRegisters_.setGeometry(rts8822::toRegisterCoordinates(pixels, scaling));
        !s) {
        return s;
    }

    rts8822::ScanFormat format;
    format.channelsPerDot = channelsIn_;
    format.depthCode = plan_.hardwareLine.depthCode;
    if (const Status s = scanRegisters_.setFormat(format); !s) {
        return s;
    }

    if (const Status s = scanRegisters_.execute(); !s) {
        return s;
    }

    started_ = true;
    return ok();
}

Status ScanSession::abort() { return finish(); }

Result<bool> ScanSession::readHardwareLine(const CancellationToken& token) {
    while (rawFilled_ < hardwareLineBytes_) {
        if (token.isCancelled()) {
            return fail(ErrorCode::Cancelled, "ScanSession: otkazano pri citanju reda");
        }
        if (transportDrained_) {
            return false;
        }

        const std::size_t want =
            std::min(chunk_.size(), hardwareLineBytes_ - rawFilled_);
        auto read = registers_.transport().bulkRead(std::span<std::byte>(chunk_.data(), want));
        if (!read) {
            return read.error();
        }
        if (read.value() == 0) {
            transportDrained_ = true;
            return false;
        }

        std::memcpy(rawLine_.data() + rawFilled_, chunk_.data(), read.value());
        rawFilled_ += read.value();
        stats_.bytesRead += read.value();
    }

    rawFilled_ = 0;
    ++stats_.hardwareLinesRead;
    return true;
}

void ScanSession::decodeChannels() {
    const std::size_t channels = static_cast<std::size_t>(channelsIn_);

    for (std::size_t pixel = 0; pixel < nativeWidth_; ++pixel) {
        for (std::size_t channel = 0; channel < channels; ++channel) {
            const std::size_t dot = pixel * channels + channel;
            std::uint16_t value = 0;
            if (wideChannel_) {
                const std::size_t byte = dot * 2;
                if (byte + 1 < rawLine_.size()) {
                    value = static_cast<std::uint16_t>(rawLine_[byte] |
                                                       (rawLine_[byte + 1] << 8));
                }
            } else if (dot < rawLine_.size()) {
                value = image::widenToFullScale(rawLine_[dot]);
            }
            planes_[channel][pixel] = value;
        }
    }

    // Shading i gamma idu po kanalu, pre svakog pretvaranja - kao u referenci,
    // gde Shading_apply radi nad sirovim kanalima.
    for (std::size_t channel = 0; channel < channels; ++channel) {
        if (shadingApplied_ && channel < image::kChannels) {
            calib::ShadingCalibration::apply(options_.shading, channel, planes_[channel]);
        }
        if (!options_.gamma.empty()) {
            (void)image::applyGamma(options_.gamma, planes_[channel]);
        }
    }
}

void ScanSession::convertToOutputFormat() {
    const bool colorOut = channelsOut_ == 3 && !lineart_;
    const bool wideOut = plan_.outputLine.depthCode == image::DepthCode::Bits16;

    if (colorOut) {
        for (std::size_t pixel = 0; pixel < nativeWidth_; ++pixel) {
            for (std::size_t channel = 0; channel < image::kChannels; ++channel) {
                const std::uint16_t value = planes_[channel][pixel];
                nativeValues_[pixel * image::kChannels + channel] =
                    wideOut ? value : static_cast<std::uint16_t>(value >> 8);
            }
        }
        return;
    }

    // Sivo: ako je ulaz vec jednokanalni, uzima se kakav jeste; ako je u boji,
    // spaja se po izabranom pravilu.
    std::vector<std::uint16_t> gray(nativeWidth_, 0);
    if (channelsIn_ == 3) {
        std::vector<std::uint16_t> planar(nativeWidth_ * image::kChannels, 0);
        for (std::size_t channel = 0; channel < image::kChannels; ++channel) {
            std::copy(planes_[channel].begin(), planes_[channel].end(),
                      planar.begin() + static_cast<std::ptrdiff_t>(channel * nativeWidth_));
        }
        (void)image::toGrayscale(planar, nativeWidth_, options_.grayMethod, gray);
    } else {
        gray = planes_[0];
    }

    if (lineart_) {
        (void)image::toLineart(gray, image::LineartThreshold{}, 16, nativeBits_);
        return;
    }

    for (std::size_t pixel = 0; pixel < nativeWidth_; ++pixel) {
        nativeValues_[pixel] =
            wideOut ? gray[pixel] : static_cast<std::uint16_t>(gray[pixel] >> 8);
    }
}

Result<bool> ScanSession::nextLine(std::span<std::uint8_t> out, const CancellationToken& token) {
    if (!started_) {
        return fail(ErrorCode::InvalidState, "ScanSession::nextLine: prolaz nije pokrenut");
    }
    if (out.size() != outputBytesPerLine()) {
        return fail(ErrorCode::InvalidArgument, "ScanSession::nextLine: pogresna duzina bafera");
    }
    if (finished_) {
        return false;
    }
    if (stats_.outputLinesProduced >= expectedLines_) {
        // Slika je gotova. Cip se MORA zaustaviti ovde - inace nastavlja
        // da skenira u prazno.
        if (const Status closed = finish(); !closed) {
            return closed.error();
        }
        return false;
    }

    for (;;) {
        if (token.isCancelled()) {
            return fail(ErrorCode::Cancelled, "ScanSession: otkazano");
        }

        auto haveLine = readHardwareLine(token);
        if (!haveLine) {
            return haveLine.error();
        }
        if (!haveLine.value()) {
            // Cip je stao pre nego sto je dao sve redove.
            if (const Status closed = finish(); !closed) {
                return closed.error();
            }
            return false;
        }

        decodeChannels();

        // Poravnanje kanala u softveru: prvih nekoliko redova samo puni cevovod.
        if (corrector_) {
            bool ready = true;
            for (std::size_t channel = 0; channel < image::kChannels; ++channel) {
                if (const Status s = corrector_->push(channel, planes_[channel]); !s) {
                    return s.error();
                }
            }
            if (!corrector_->hasOutput()) {
                ++stats_.alignmentLinesConsumed;
                continue;
            }
            if (const Status s = corrector_->pop(alignedLine_); !s) {
                return s.error();
            }
            for (std::size_t channel = 0; channel < image::kChannels; ++channel) {
                std::copy_n(alignedLine_.begin() +
                                static_cast<std::ptrdiff_t>(channel * nativeWidth_),
                            nativeWidth_, planes_[channel].begin());
            }
            (void)ready;
        }

        convertToOutputFormat();

        // Vodoravno smanjivanje.
        if (plan_.resize == ResizeType::Decrease) {
            if (lineart_) {
                if (const Status s = image::resizeLineartDown(
                        nativeBits_, nativeWidth_, plan_.nativeResolution, resizedBits_,
                        outputWidth_, plan_.requestedResolution);
                    !s) {
                    return s.error();
                }
            } else {
                const std::size_t channels = static_cast<std::size_t>(channelsOut_);
                std::vector<std::uint16_t> from(nativeWidth_, 0);
                std::vector<std::uint16_t> to(outputWidth_, 0);
                for (std::size_t channel = 0; channel < channels; ++channel) {
                    for (std::size_t pixel = 0; pixel < nativeWidth_; ++pixel) {
                        from[pixel] = nativeValues_[pixel * channels + channel];
                    }
                    if (const Status s = image::resizeLineDown(from, plan_.nativeResolution, to,
                                                               plan_.requestedResolution);
                        !s) {
                        return s.error();
                    }
                    for (std::size_t pixel = 0; pixel < outputWidth_; ++pixel) {
                        resizedValues_[pixel * channels + channel] = to[pixel];
                    }
                }
            }

            // Uspravno smanjivanje: i ono guta redove pre prvog izlaznog.
            if (lineart_) {
                if (const Status s = lineartResampler_->push(resizedBits_); !s) {
                    return s.error();
                }
                if (!lineartResampler_->hasOutput()) {
                    ++stats_.resampleLinesConsumed;
                    continue;
                }
                if (const Status s = lineartResampler_->pop(resizedBits_); !s) {
                    return s.error();
                }
            } else {
                if (const Status s = resampler_->push(resizedValues_); !s) {
                    return s.error();
                }
                if (!resampler_->hasOutput()) {
                    ++stats_.resampleLinesConsumed;
                    continue;
                }
                if (const Status s = resampler_->pop(resizedValues_); !s) {
                    return s.error();
                }
            }
        }

        break;
    }

    // Pakovanje u izlazni bafer.
    if (lineart_) {
        const auto& source = plan_.resize == ResizeType::Decrease ? resizedBits_ : nativeBits_;
        std::copy_n(source.begin(), std::min(out.size(), source.size()), out.begin());
    } else {
        const auto& source =
            plan_.resize == ResizeType::Decrease ? resizedValues_ : nativeValues_;
        const bool wideOut = plan_.outputLine.depthCode == image::DepthCode::Bits16;
        const std::size_t values =
            std::min(source.size(), wideOut ? out.size() / 2 : out.size());

        for (std::size_t i = 0; i < values; ++i) {
            if (wideOut) {
                out[i * 2] = static_cast<std::uint8_t>(source[i] & 0xFF);
                out[i * 2 + 1] = static_cast<std::uint8_t>(source[i] >> 8);
            } else {
                out[i] = static_cast<std::uint8_t>(source[i] & 0xFF);
            }
        }
    }

    ++stats_.outputLinesProduced;
    return true;
}

}  // namespace g2710::scan
