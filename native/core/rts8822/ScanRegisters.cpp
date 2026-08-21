#include "ScanRegisters.h"

#include "Registers.h"

#include <algorithm>

namespace g2710::rts8822 {
namespace {

// Najveca uspravna koordinata: 16 bita u paru plus 4 bita u niblu.
constexpr int kMaxVertical = 0xFFFFF;
constexpr int kMaxHorizontal = 0xFFFF;

constexpr std::uint8_t highNibbleOf(int value) noexcept {
    return static_cast<std::uint8_t>((value >> 16) & 0x0F);
}

}  // namespace

std::size_t bytesPerLine(const ScanGeometry& geometry, const ScanFormat& format) noexcept {
    if (geometry.width <= 0 || format.channelsPerDot <= 0) {
        return 0;
    }
    // rts8822.c:8773 - channels_per_line = channels_per_dot * width
    const std::size_t channels =
        static_cast<std::size_t>(format.channelsPerDot) * static_cast<std::size_t>(geometry.width);

    switch (format.depthCode) {
        case image::DepthCode::Lineart:
            return (channels + 7) / 8;
        case image::DepthCode::Bits12:
        case image::DepthCode::Bits16:
            return channels * 2;
        case image::DepthCode::Bits8:
            break;
    }
    return channels;
}

ScanGeometry toRegisterCoordinates(const ScanGeometry& pixels,
                                   const CoordinateScaling& scaling) noexcept {
    ScanGeometry out;
    if (!scaling.valid()) {
        return out;
    }

    // rts8822.c:9145 - nula se podize na jedan PRE mnozenja.
    const int left = pixels.left == 0 ? 1 : pixels.left;
    const int top = pixels.top == 0 ? 1 : pixels.top;

    out.left = left * scaling.resolutionRatio;
    out.width = pixels.width * scaling.resolutionRatio;

    // rts8822.c:9159 - leva ivica mora biti NEPARNA. Referenca to ne
    // obrazlaze; prenosi se doslovno jer se tice parnih i neparnih piksela
    // senzora, gde pomeraj za jedan menja koji lanac cita koji piksel.
    if ((out.left & 1) == 0) {
        ++out.left;
    }

    out.top = top * scaling.dummyLine;
    out.height = (scaling.lineOffsetPadding + pixels.height + scaling.softwareLineDistance) *
                 scaling.dummyLine;
    return out;
}

ScanGeometry fromRegisterCoordinates(const ScanGeometry& registers,
                                     const CoordinateScaling& scaling) noexcept {
    ScanGeometry out;
    if (!scaling.valid()) {
        return out;
    }
    out.left = registers.left / scaling.resolutionRatio;
    out.width = registers.width / scaling.resolutionRatio;
    out.top = registers.top / scaling.dummyLine;
    out.height = registers.height / scaling.dummyLine;
    return out;
}

Status ScanRegisters::setGeometry(const ScanGeometry& geometry) {
    if (const Status allowed = gate_.require(SafetyLevel::Acquire, "scan.setGeometry");
        !allowed) {
        return allowed;
    }
    if (geometry.width <= 0 || geometry.height <= 0) {
        return fail(ErrorCode::InvalidArgument, "scan.setGeometry: prazna oblast");
    }
    if (geometry.left < 0 || geometry.top < 0) {
        return fail(ErrorCode::InvalidArgument, "scan.setGeometry: negativna koordinata");
    }
    if (geometry.right() > kMaxHorizontal) {
        return fail(ErrorCode::InvalidArgument, "scan.setGeometry: desna ivica ne staje u 16 bita");
    }
    if (geometry.bottom() > kMaxVertical) {
        return fail(ErrorCode::InvalidArgument, "scan.setGeometry: donja ivica ne staje u 20 bita");
    }

    // rts8822.c:9239. Redosled je isti kao u referenci; nizi par pa visi nibl.
    if (const Status s =
            registers_.writeWord(reg::kScanLeft, static_cast<std::uint16_t>(geometry.left));
        !s) {
        return s;
    }
    if (const Status s =
            registers_.writeWord(reg::kScanRight, static_cast<std::uint16_t>(geometry.right()));
        !s) {
        return s;
    }
    if (const Status s =
            registers_.writeWord(reg::kScanTop, static_cast<std::uint16_t>(geometry.top));
        !s) {
        return s;
    }
    if (const Status s =
            registers_.writeWord(reg::kScanBottom, static_cast<std::uint16_t>(geometry.bottom()));
        !s) {
        return s;
    }

    // Oba nibla zive u istom bajtu, pa se cita jednom i upisuje jednom.
    auto high = registers_.readByte(reg::kScanVerticalHigh);
    if (!high) {
        return high.error();
    }
    std::uint8_t value = high.value();
    value = bitsetValue(value, reg::kScanTopHighMask, highNibbleOf(geometry.top));
    value = bitsetValue(value, reg::kScanBottomHighMask, highNibbleOf(geometry.bottom()));
    return registers_.writeByte(reg::kScanVerticalHigh, value);
}

Result<ScanGeometry> ScanRegisters::geometry() {
    if (const Status allowed = gate_.require(SafetyLevel::ReadOnly, "scan.geometry"); !allowed) {
        return allowed.error();
    }

    auto left = registers_.readWord(reg::kScanLeft);
    if (!left) {
        return left.error();
    }
    auto right = registers_.readWord(reg::kScanRight);
    if (!right) {
        return right.error();
    }
    auto top = registers_.readWord(reg::kScanTop);
    if (!top) {
        return top.error();
    }
    auto bottom = registers_.readWord(reg::kScanBottom);
    if (!bottom) {
        return bottom.error();
    }
    auto high = registers_.readByte(reg::kScanVerticalHigh);
    if (!high) {
        return high.error();
    }

    const int topHigh = bitsetGet(high.value(), reg::kScanTopHighMask) << 16;
    const int bottomHigh = bitsetGet(high.value(), reg::kScanBottomHighMask) << 16;

    ScanGeometry result;
    result.left = left.value();
    result.top = top.value() | topHigh;
    result.width = static_cast<int>(right.value()) - result.left;
    result.height = (static_cast<int>(bottom.value()) | bottomHigh) - result.top;
    return result;
}

Status ScanRegisters::setFormat(const ScanFormat& format) {
    if (const Status allowed = gate_.require(SafetyLevel::Acquire, "scan.setFormat"); !allowed) {
        return allowed;
    }
    if (format.channelsPerDot < 1 || format.channelsPerDot > 3) {
        return fail(ErrorCode::InvalidArgument, "scan.setFormat: kanala po tacki mora biti 1..3");
    }

    auto channels = registers_.readByte(reg::kChannelsPerDot);
    if (!channels) {
        return channels.error();
    }
    const std::uint8_t updated =
        bitsetValue(channels.value(), reg::kChannelsPerDotMask,
                    static_cast<std::uint8_t>(format.channelsPerDot));
    if (const Status s = registers_.writeByte(reg::kChannelsPerDot, updated); !s) {
        return s;
    }

    auto depth = registers_.readByte(reg::kDepthCode);
    if (!depth) {
        return depth.error();
    }
    const std::uint8_t depthValue =
        bitsetValue(depth.value(), reg::kDepthCodeMask,
                    static_cast<std::uint8_t>(format.depthCode));
    if (const Status s = registers_.writeByte(reg::kDepthCode, depthValue); !s) {
        return s;
    }

    // Sirina kanala. rts8822.c:9190 postavlja bit 0x40, :9194 obara 0x08;
    // citanje na :7731 trazi oba uslova.
    //
    // Bez ovog upisa cip isporucuje OSAM bita ma sta pisalo u polju dubine,
    // pa bi 16-bitni prolaz tiho dao pogresne podatke.
    const bool wide = format.depthCode == image::DepthCode::Bits16 ||
                      format.depthCode == image::DepthCode::Bits12;

    auto channelSize = registers_.readByte(reg::kChannelSize);
    if (!channelSize) {
        return channelSize.error();
    }
    std::uint8_t sizeByte = channelSize.value();
    if (wide) {
        sizeByte = static_cast<std::uint8_t>(sizeByte | reg::kChannelSizeWideBit);
        sizeByte = static_cast<std::uint8_t>(sizeByte & ~reg::kChannelSizeNarrowBit);
    } else {
        sizeByte = static_cast<std::uint8_t>(sizeByte & ~reg::kChannelSizeWideBit);
    }
    if (const Status s = registers_.writeByte(reg::kChannelSize, sizeByte); !s) {
        return s;
    }

    // rts8822.c:8390 - referenca oba praga upisuje bezuslovno, i u rezimima
    // gde se ne koriste. Preneto doslovno.
    if (const Status s = registers_.writeWord(
            reg::kThresholdHigh, static_cast<std::uint16_t>(format.threshold.high));
        !s) {
        return s;
    }
    return registers_.writeWord(reg::kThresholdLow,
                                static_cast<std::uint16_t>(format.threshold.low));
}

Result<ScanFormat> ScanRegisters::format() {
    if (const Status allowed = gate_.require(SafetyLevel::ReadOnly, "scan.format"); !allowed) {
        return allowed.error();
    }

    auto channels = registers_.readByte(reg::kChannelsPerDot);
    if (!channels) {
        return channels.error();
    }
    auto depth = registers_.readByte(reg::kDepthCode);
    if (!depth) {
        return depth.error();
    }
    auto high = registers_.readWord(reg::kThresholdHigh);
    if (!high) {
        return high.error();
    }
    auto low = registers_.readWord(reg::kThresholdLow);
    if (!low) {
        return low.error();
    }

    auto channelSize = registers_.readByte(reg::kChannelSize);
    if (!channelSize) {
        return channelSize.error();
    }

    ScanFormat result;
    result.wideChannel = (channelSize.value() & reg::kChannelSizeWideBit) != 0 &&
                         (channelSize.value() & reg::kChannelSizeNarrowBit) == 0;
    result.channelsPerDot = bitsetGet(channels.value(), reg::kChannelsPerDotMask);
    result.depthCode =
        static_cast<image::DepthCode>(bitsetGet(depth.value(), reg::kDepthCodeMask));
    result.threshold.high = high.value();
    result.threshold.low = low.value();
    return result;
}

namespace {

// Pet polja u redosledu u kome ih referenca upisuje.
struct OffsetField {
    std::uint16_t address;
    int image::LineOffsetRegisters::* member;
};

constexpr OffsetField kOffsetFields[] = {
    {reg::kLineOffsetEvenOdd, &image::LineOffsetRegisters::evenOdd},
    {reg::kLineOffsetDistance, &image::LineOffsetRegisters::lineDistance},
    {reg::kLineOffsetDistancePlus, &image::LineOffsetRegisters::lineDistancePlusEvenOdd},
    {reg::kLineOffsetDouble, &image::LineOffsetRegisters::doubleLineDistance},
    {reg::kLineOffsetDoublePlus, &image::LineOffsetRegisters::doublePlusEvenOdd},
};

}  // namespace

Status ScanRegisters::setLineOffsets(const image::LineOffsetRegisters& offsets) {
    if (const Status allowed = gate_.require(SafetyLevel::Acquire, "scan.setLineOffsets");
        !allowed) {
        return allowed;
    }

    for (const auto& field : kOffsetFields) {
        auto current = registers_.readByte(field.address);
        if (!current) {
            return current.error();
        }
        // Maska 0x3F, kao data_bitset u referenci. Vrednost preko 63 se odseca
        // i taj gubitak se NE prijavljuje - vidi komentar u zaglavlju.
        const std::uint8_t updated =
            bitsetValue(current.value(), reg::kLineOffsetMask,
                        static_cast<std::uint8_t>(offsets.*field.member));
        if (const Status s = registers_.writeByte(field.address, updated); !s) {
            return s;
        }
    }
    return ok();
}

Result<image::LineOffsetRegisters> ScanRegisters::lineOffsets() {
    if (const Status allowed = gate_.require(SafetyLevel::ReadOnly, "scan.lineOffsets");
        !allowed) {
        return allowed.error();
    }

    image::LineOffsetRegisters result;
    for (const auto& field : kOffsetFields) {
        auto value = registers_.readByte(field.address);
        if (!value) {
            return value.error();
        }
        result.*field.member = bitsetGet(value.value(), reg::kLineOffsetMask);
    }
    return result;
}

Status ScanRegisters::clearLineOffsets() {
    return setLineOffsets(image::LineOffsetRegisters{});
}

Status ScanRegisters::setResolutionRatio(int ratio) {
    if (const Status allowed = gate_.require(SafetyLevel::Acquire, "scan.setResolutionRatio");
        !allowed) {
        return allowed;
    }
    if (ratio < 1 || ratio > reg::kResolutionRatioMask) {
        return fail(ErrorCode::InvalidArgument, "scan.setResolutionRatio: van 1..31");
    }
    auto current = registers_.readByte(reg::kResolutionRatio);
    if (!current) {
        return current.error();
    }
    return registers_.writeByte(
        reg::kResolutionRatio,
        bitsetValue(current.value(), reg::kResolutionRatioMask,
                    static_cast<std::uint8_t>(ratio)));
}

Result<int> ScanRegisters::resolutionRatio() {
    if (const Status allowed = gate_.require(SafetyLevel::ReadOnly, "scan.resolutionRatio");
        !allowed) {
        return allowed.error();
    }
    auto value = registers_.readByte(reg::kResolutionRatio);
    if (!value) {
        return value.error();
    }
    return static_cast<int>(bitsetGet(value.value(), reg::kResolutionRatioMask));
}

Status ScanRegisters::setDummyLine(int dummyLine) {
    if (const Status allowed = gate_.require(SafetyLevel::Acquire, "scan.setDummyLine");
        !allowed) {
        return allowed;
    }
    if (dummyLine < 1 || dummyLine > 15) {
        return fail(ErrorCode::InvalidArgument, "scan.setDummyLine: van 1..15");
    }
    auto current = registers_.readByte(reg::kDummyLine);
    if (!current) {
        return current.error();
    }
    return registers_.writeByte(
        reg::kDummyLine,
        bitsetValue(current.value(), reg::kDummyLineMask,
                    static_cast<std::uint8_t>(dummyLine)));
}

Result<int> ScanRegisters::dummyLine() {
    if (const Status allowed = gate_.require(SafetyLevel::ReadOnly, "scan.dummyLine"); !allowed) {
        return allowed.error();
    }
    auto value = registers_.readByte(reg::kDummyLine);
    if (!value) {
        return value.error();
    }
    return static_cast<int>(bitsetGet(value.value(), reg::kDummyLineMask));
}

Status ScanRegisters::execute() {
    if (const Status allowed = gate_.require(SafetyLevel::FullScan, "scan.execute"); !allowed) {
        return allowed;
    }

    // rts8822.c:3947. Sest upisa, naizmenicno kroz dva registra. Skracivanje
    // sekvence je najlaksi nacin da se dobije cip koji "ne pocinje".
    auto control = registers_.readByte(reg::kControl);
    if (!control) {
        return control.error();
    }
    auto channels = registers_.readByte(reg::kCcdChannelsHigh);
    if (!channels) {
        return channels.error();
    }

    std::uint8_t e800 = control.value();
    std::uint8_t e813 = channels.value();

    e813 &= static_cast<std::uint8_t>(~reg::kControlWarmResetBit);
    if (const Status s = registers_.writeByte(reg::kCcdChannelsHigh, e813); !s) {
        return s;
    }
    e800 |= reg::kControlWarmResetBit;
    if (const Status s = registers_.writeByte(reg::kControl, e800); !s) {
        return s;
    }
    e813 |= reg::kControlWarmResetBit;
    if (const Status s = registers_.writeByte(reg::kCcdChannelsHigh, e813); !s) {
        return s;
    }
    e800 &= static_cast<std::uint8_t>(~reg::kControlWarmResetBit);
    if (const Status s = registers_.writeByte(reg::kControl, e800); !s) {
        return s;
    }

    e800 |= reg::kControlExecutingBit;
    return registers_.writeByte(reg::kControl, e800);
}

Result<bool> ScanRegisters::isExecuting() {
    if (const Status allowed = gate_.require(SafetyLevel::ReadOnly, "scan.isExecuting");
        !allowed) {
        return allowed.error();
    }
    auto value = registers_.readByte(reg::kControl);
    if (!value) {
        return value.error();
    }
    return (value.value() & reg::kControlExecutingBit) != 0;
}

Status ScanRegisters::waitScanEnd(std::chrono::milliseconds timeout) {
    if (const Status allowed = gate_.require(SafetyLevel::ReadOnly, "scan.waitScanEnd");
        !allowed) {
        return allowed;
    }

    // ODSTUPANJE OD REFERENCE, namerno.
    //
    // RTS_WaitScanEnd (rts8822.c:3868) vraca OK i kada je rok istekao - njegov
    // komentar to i kaze: "returns 0 if ok or timeout". Time se gubi razlika
    // izmedju "skeniranje je zavrseno" i "odustali smo posle petnaest sekundi".
    //
    // Na tudjem racunaru, gde jedini trag ostaje u izvestaju, to je bas ona
    // razlika koja se trazi. Zato ovde Timeout izlazi kao Timeout.
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    for (;;) {
        auto value = registers_.readByte(reg::kControl);
        if (!value) {
            return value.error();
        }
        if ((value.value() & reg::kControlExecutingBit) == 0) {
            return ok();
        }
        if (std::chrono::steady_clock::now() >= deadline) {
            return fail(ErrorCode::Timeout, "scan.waitScanEnd");
        }
    }
}

Status ScanRegisters::warmReset() {
    if (const Status allowed = gate_.require(SafetyLevel::Acquire, "scan.warmReset"); !allowed) {
        return allowed;
    }

    // rts8822.c:3913. Maska 0x3F obara i bit izvrsavanja, ne samo reset bit.
    auto value = registers_.readByte(reg::kControl);
    if (!value) {
        return value.error();
    }
    const std::uint8_t set =
        static_cast<std::uint8_t>((value.value() & 0x3F) | reg::kControlWarmResetBit);
    if (const Status s = registers_.writeByte(reg::kControl, set); !s) {
        return s;
    }
    const std::uint8_t cleared =
        static_cast<std::uint8_t>(set & ~reg::kControlWarmResetBit);
    return registers_.writeByte(reg::kControl, cleared);
}

}  // namespace g2710::rts8822
