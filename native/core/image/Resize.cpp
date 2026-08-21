#include "Resize.h"

#include <algorithm>
#include <limits>

namespace g2710::image {
namespace {

constexpr std::int64_t kMaxValue = std::numeric_limits<std::uint16_t>::max();

std::uint16_t clampToSample(std::int64_t value) noexcept {
    return static_cast<std::uint16_t>(std::clamp<std::int64_t>(value, 0, kMaxValue));
}

bool bitAt(std::span<const std::uint8_t> packed, std::size_t index) noexcept {
    const std::size_t byte = index / 8;
    if (byte >= packed.size()) {
        return false;
    }
    return (packed[byte] & (0x80u >> (index % 8))) != 0;
}

void setBit(std::span<std::uint8_t> packed, std::size_t index) noexcept {
    const std::size_t byte = index / 8;
    if (byte < packed.size()) {
        packed[byte] |= static_cast<std::uint8_t>(0x80u >> (index % 8));
    }
}

}  // namespace

std::size_t resizedWidth(std::size_t fromWidth, int fromResolution,
                         int toResolution) noexcept {
    if (fromResolution <= 0 || toResolution <= 0) {
        return 0;
    }
    return (fromWidth * static_cast<std::size_t>(toResolution)) /
           static_cast<std::size_t>(fromResolution);
}

Status resizeLineDown(std::span<const std::uint16_t> from, int fromResolution,
                      std::span<std::uint16_t> to, int toResolution) {
    if (fromResolution <= 0 || toResolution <= 0) {
        return fail(ErrorCode::InvalidArgument, "resizeLineDown: rezolucija mora biti pozitivna");
    }
    if (toResolution > fromResolution) {
        return fail(ErrorCode::InvalidArgument, "resizeLineDown: samo smanjivanje");
    }
    if (from.empty()) {
        return fail(ErrorCode::InvalidArgument, "resizeLineDown: prazan ulaz");
    }
    if (to.empty()) {
        return ok();
    }

    const std::int64_t fromRes = fromResolution;
    const std::int64_t toRes = toResolution;
    const std::size_t fromWidth = from.size();

    // Verna kopija petlje iz rts8822.c:5786. `accumulator` je color[C],
    // `carry` je rescont.
    std::size_t source = 0;
    std::size_t consumed = 0;
    std::int64_t accumulator = 0;
    std::int64_t carry = 0;

    for (std::size_t out = 0; out < to.size();) {
        ++consumed;
        if (consumed > fromWidth) {
            // Referenca vraca pokazivac za jedan piksel unazad kada ulaz
            // presusi, pa se poslednji piksel ponavlja umesto da se cita van
            // bafera.
            source = source > 0 ? source - 1 : 0;
        }
        if (source >= fromWidth) {
            source = fromWidth - 1;
        }

        const std::int64_t sample = from[source];
        carry += toRes;

        if (carry < fromRes) {
            accumulator += sample * toRes;
        } else {
            const std::int64_t overshoot = carry - fromRes;
            const std::int64_t partial = toRes - overshoot;
            to[out] = clampToSample((sample * partial + accumulator) / fromRes);
            accumulator = sample * overshoot;
            carry = overshoot;
            ++out;
        }
        ++source;
    }
    return ok();
}

Status resizeLineartDown(std::span<const std::uint8_t> from, std::size_t fromWidth,
                         int fromResolution, std::span<std::uint8_t> to,
                         std::size_t toWidth, int toResolution) {
    if (fromResolution <= 0 || toResolution <= 0) {
        return fail(ErrorCode::InvalidArgument, "resizeLineartDown: rezolucija mora biti pozitivna");
    }
    if (toResolution > fromResolution) {
        return fail(ErrorCode::InvalidArgument, "resizeLineartDown: samo smanjivanje");
    }
    if (from.size() < (fromWidth + 7) / 8) {
        return fail(ErrorCode::InvalidArgument, "resizeLineartDown: ulazni bafer prekratak");
    }
    if (to.size() < (toWidth + 7) / 8) {
        return fail(ErrorCode::InvalidArgument, "resizeLineartDown: izlazni bafer prekratak");
    }
    std::fill(to.begin(), to.end(), std::uint8_t{0});
    if (toWidth == 0 || fromWidth == 0) {
        return ok();
    }

    const std::int64_t fromRes = fromResolution;
    const std::int64_t toRes = toResolution;

    // rts8822.c:5824. `coverage` je rescont2 - koliko je "crnog" upalo u
    // tekuci izlazni piksel; prag je pola izlaznog koraka.
    std::size_t source = 0;
    std::size_t out = 0;
    std::int64_t carry = 0;
    std::int64_t coverage = 0;

    while (out < toWidth) {
        const bool set = bitAt(from, std::min(source, fromWidth - 1));
        carry += toRes;

        if (carry < fromRes) {
            if (set) {
                coverage += toRes;
            }
        } else {
            carry -= fromRes;
            if (set) {
                coverage += toRes - carry;
            }
            if (coverage > toRes / 2) {
                setBit(to, out);
            }
            coverage = set ? carry : 0;
            ++out;
        }
        ++source;
    }
    return ok();
}

// --- uspravno ----------------------------------------------------------------

VerticalResampler::VerticalResampler(int fromResolution, int toResolution,
                                     std::size_t valuesPerLine)
    : from_(fromResolution), to_(toResolution), values_(valuesPerLine) {
    if (valid()) {
        previous_.assign(values_, 0);
        current_.assign(values_, 0);
        output_.assign(values_, 0);
    }
}

void VerticalResampler::reset() {
    std::fill(previous_.begin(), previous_.end(), std::uint16_t{0});
    std::fill(current_.begin(), current_.end(), std::uint16_t{0});
    std::fill(output_.begin(), output_.end(), std::uint16_t{0});
    accumulator_ = 0;
    hasPrevious_ = false;
    ready_ = false;
    consumed_ = 0;
    produced_ = 0;
}

std::size_t VerticalResampler::expectedOutputLines(std::size_t inputLines,
                                                   int fromResolution,
                                                   int toResolution) noexcept {
    if (fromResolution <= 0 || toResolution <= 0) {
        return 0;
    }
    // Isti akumulator, samo bez podataka.
    std::size_t produced = 0;
    std::int64_t accumulator = 0;
    bool hasPrevious = false;
    for (std::size_t i = 0; i < inputLines; ++i) {
        accumulator += toResolution;
        if (accumulator > fromResolution) {
            accumulator -= fromResolution;
            if (hasPrevious) {
                ++produced;
            }
        }
        hasPrevious = true;
    }
    return produced;
}

Status VerticalResampler::push(std::span<const std::uint16_t> line) {
    if (!valid()) {
        return fail(ErrorCode::InvalidArgument, "VerticalResampler: neispravna konfiguracija");
    }
    if (line.size() != values_) {
        return fail(ErrorCode::InvalidArgument, "VerticalResampler: pogresna duzina reda");
    }
    if (ready_) {
        return fail(ErrorCode::InvalidState, "VerticalResampler: prethodni izlaz nije preuzet");
    }

    std::copy(line.begin(), line.end(), current_.begin());
    ++consumed_;
    accumulator_ += to_;

    if (accumulator_ > from_) {
        accumulator_ -= from_;
        if (hasPrevious_) {
            // rts8822.c:6893 - tezina na tekucem redu je (from - rescount),
            // na prethodnom rescount.
            const std::int64_t weightCurrent = from_ - accumulator_;
            const std::int64_t weightPrevious = accumulator_;
            for (std::size_t i = 0; i < values_; ++i) {
                const std::int64_t mixed =
                    (weightCurrent * current_[i] + weightPrevious * previous_[i]) / from_;
                output_[i] = clampToSample(mixed);
            }
            ready_ = true;
            ++produced_;
        }
    }

    previous_.swap(current_);
    hasPrevious_ = true;
    return ok();
}

Status VerticalResampler::pop(std::span<std::uint16_t> out) {
    if (!ready_) {
        return fail(ErrorCode::InvalidState, "VerticalResampler: nema spremnog reda");
    }
    if (out.size() != values_) {
        return fail(ErrorCode::InvalidArgument, "VerticalResampler: pogresna duzina izlaza");
    }
    std::copy(output_.begin(), output_.end(), out.begin());
    ready_ = false;
    return ok();
}

// --- uspravno, lineart --------------------------------------------------------

VerticalLineartResampler::VerticalLineartResampler(int fromResolution, int toResolution,
                                                   std::size_t widthInPixels)
    : from_(fromResolution), to_(toResolution), width_(widthInPixels) {
    if (valid()) {
        previous_.assign(bytesPerLine(), 0);
        current_.assign(bytesPerLine(), 0);
        output_.assign(bytesPerLine(), 0);
    }
}

void VerticalLineartResampler::reset() {
    std::fill(previous_.begin(), previous_.end(), std::uint8_t{0});
    std::fill(current_.begin(), current_.end(), std::uint8_t{0});
    std::fill(output_.begin(), output_.end(), std::uint8_t{0});
    accumulator_ = 0;
    hasPrevious_ = false;
    ready_ = false;
    consumed_ = 0;
    produced_ = 0;
}

Status VerticalLineartResampler::push(std::span<const std::uint8_t> line) {
    if (!valid()) {
        return fail(ErrorCode::InvalidArgument,
                    "VerticalLineartResampler: neispravna konfiguracija");
    }
    if (line.size() != bytesPerLine()) {
        return fail(ErrorCode::InvalidArgument,
                    "VerticalLineartResampler: pogresna duzina reda");
    }
    if (ready_) {
        return fail(ErrorCode::InvalidState,
                    "VerticalLineartResampler: prethodni izlaz nije preuzet");
    }

    std::copy(line.begin(), line.end(), current_.begin());
    ++consumed_;
    accumulator_ += to_;

    if (accumulator_ > from_) {
        accumulator_ -= from_;
        if (hasPrevious_) {
            // rts8822.c:6853. Tezina prethodnog reda je rescount, tekuceg
            // (from - rescount); prag je izlazna rezolucija.
            std::fill(output_.begin(), output_.end(), std::uint8_t{0});
            for (std::size_t pixel = 0; pixel < width_; ++pixel) {
                std::int64_t coverage = bitAt(previous_, pixel) ? accumulator_ : 0;
                if (bitAt(current_, pixel)) {
                    coverage += from_ - accumulator_;
                }
                if (coverage > to_) {
                    setBit(output_, pixel);
                }
            }
            ready_ = true;
            ++produced_;
        }
    }

    previous_.swap(current_);
    hasPrevious_ = true;
    return ok();
}

Status VerticalLineartResampler::pop(std::span<std::uint8_t> out) {
    if (!ready_) {
        return fail(ErrorCode::InvalidState, "VerticalLineartResampler: nema spremnog reda");
    }
    if (out.size() != bytesPerLine()) {
        return fail(ErrorCode::InvalidArgument,
                    "VerticalLineartResampler: pogresna duzina izlaza");
    }
    std::copy(output_.begin(), output_.end(), out.begin());
    ready_ = false;
    return ok();
}

}  // namespace g2710::image
