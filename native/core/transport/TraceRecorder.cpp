#include "TraceRecorder.h"

#include <cstdio>

namespace g2710 {
namespace {

void appendHex(std::string* out, const std::vector<std::uint8_t>& data) {
    char buffer[4];
    for (std::size_t i = 0; i < data.size(); ++i) {
        std::snprintf(buffer, sizeof(buffer), "%02x ", data[i]);
        out->append(buffer);
    }
}

}  // namespace

Status TraceRecorder::controlIn(std::uint16_t address, Command command,
                                std::span<std::byte> buffer) {
    const Status status = inner_.controlIn(address, command, buffer);

    TraceEntry entry;
    entry.kind = TraceEntry::Kind::ControlIn;
    entry.address = address;
    entry.command = command;
    entry.result = status.code();
    entry.data.resize(buffer.size());
    for (std::size_t i = 0; i < buffer.size(); ++i) {
        entry.data[i] = static_cast<std::uint8_t>(buffer[i]);
    }
    entries_.push_back(std::move(entry));

    return status;
}

Status TraceRecorder::controlOut(std::uint16_t address, Command command,
                                 std::span<const std::byte> buffer) {
    TraceEntry entry;
    entry.kind = TraceEntry::Kind::ControlOut;
    entry.address = address;
    entry.command = command;
    entry.data.resize(buffer.size());
    for (std::size_t i = 0; i < buffer.size(); ++i) {
        entry.data[i] = static_cast<std::uint8_t>(buffer[i]);
    }

    const Status status = inner_.controlOut(address, command, buffer);
    entry.result = status.code();
    entries_.push_back(std::move(entry));

    return status;
}

Result<std::size_t> TraceRecorder::bulkRead(std::span<std::byte> buffer) {
    auto result = inner_.bulkRead(buffer);

    TraceEntry entry;
    entry.kind = TraceEntry::Kind::BulkRead;
    entry.result = result.code();
    const std::size_t count = result.hasValue() ? result.value() : 0;
    entry.data.resize(count);
    for (std::size_t i = 0; i < count; ++i) {
        entry.data[i] = static_cast<std::uint8_t>(buffer[i]);
    }
    entries_.push_back(std::move(entry));

    return result;
}

Status TraceRecorder::bulkWrite(std::span<const std::byte> buffer) {
    TraceEntry entry;
    entry.kind = TraceEntry::Kind::BulkWrite;
    entry.data.resize(buffer.size());
    for (std::size_t i = 0; i < buffer.size(); ++i) {
        entry.data[i] = static_cast<std::uint8_t>(buffer[i]);
    }

    const Status status = inner_.bulkWrite(buffer);
    entry.result = status.code();
    entries_.push_back(std::move(entry));

    return status;
}

Result<std::uint32_t> TraceRecorder::waitEvent() {
    auto result = inner_.waitEvent();

    TraceEntry entry;
    entry.kind = TraceEntry::Kind::WaitEvent;
    entry.result = result.code();
    if (result.hasValue()) {
        const std::uint32_t mask = result.value();
        entry.data = {static_cast<std::uint8_t>(mask & 0xFF),
                      static_cast<std::uint8_t>((mask >> 8) & 0xFF),
                      static_cast<std::uint8_t>((mask >> 16) & 0xFF),
                      static_cast<std::uint8_t>((mask >> 24) & 0xFF)};
    }
    entries_.push_back(std::move(entry));

    return result;
}

Status TraceRecorder::resetPipe(PipeKind pipe) {
    const Status status = inner_.resetPipe(pipe);

    TraceEntry entry;
    entry.kind = TraceEntry::Kind::ResetPipe;
    entry.address = static_cast<std::uint16_t>(pipe);
    entry.result = status.code();
    entries_.push_back(std::move(entry));

    return status;
}

Status TraceRecorder::setTimeouts(const Timeouts& timeouts) {
    const Status status = inner_.setTimeouts(timeouts);

    TraceEntry entry;
    entry.kind = TraceEntry::Kind::SetTimeouts;
    entry.result = status.code();
    entries_.push_back(std::move(entry));

    return status;
}

Result<PipeConfiguration> TraceRecorder::pipeConfiguration() {
    auto result = inner_.pipeConfiguration();

    TraceEntry entry;
    entry.kind = TraceEntry::Kind::PipeConfiguration;
    entry.result = result.code();
    entries_.push_back(std::move(entry));

    return result;
}

void TraceRecorder::cancel() noexcept {
    TraceEntry entry;
    entry.kind = TraceEntry::Kind::Cancel;
    entries_.push_back(std::move(entry));
    inner_.cancel();
}

Status TraceRecorder::reopen() {
    const Status status = inner_.reopen();

    TraceEntry entry;
    entry.kind = TraceEntry::Kind::Reopen;
    entry.result = status.code();
    entries_.push_back(std::move(entry));

    return status;
}

std::string TraceRecorder::format(bool withData) const {
    std::string out;
    char line[128];

    for (const TraceEntry& entry : entries_) {
        switch (entry.kind) {
            case TraceEntry::Kind::ControlOut:
                // Isti oblik kao DBG(DBG_CTL, "CTL DO: 40 04 %04x %04x %04x").
                std::snprintf(line, sizeof(line), "CTL DO: 40 04 %04x %04x %04x\n",
                              entry.address, static_cast<unsigned>(entry.command),
                              static_cast<unsigned>(entry.data.size()));
                break;

            case TraceEntry::Kind::ControlIn:
                std::snprintf(line, sizeof(line), "CTL DI: c0 04 %04x %04x %04x\n",
                              entry.address, static_cast<unsigned>(entry.command),
                              static_cast<unsigned>(entry.data.size()));
                break;

            case TraceEntry::Kind::BulkWrite:
                std::snprintf(line, sizeof(line), "BLK DO: %u bytes\n",
                              static_cast<unsigned>(entry.data.size()));
                break;

            case TraceEntry::Kind::BulkRead:
                std::snprintf(line, sizeof(line), "BLK DI: %u bytes\n",
                              static_cast<unsigned>(entry.data.size()));
                break;

            case TraceEntry::Kind::WaitEvent:
                std::snprintf(line, sizeof(line), "EVT DI: %s\n",
                              toString(entry.result));
                break;

            case TraceEntry::Kind::ResetPipe:
                std::snprintf(line, sizeof(line), "PIPE RESET: %u\n", entry.address);
                break;

            case TraceEntry::Kind::SetTimeouts:
                std::snprintf(line, sizeof(line), "SET TIMEOUTS\n");
                break;

            case TraceEntry::Kind::PipeConfiguration:
                std::snprintf(line, sizeof(line), "PIPE CONFIG\n");
                break;

            case TraceEntry::Kind::Cancel:
                std::snprintf(line, sizeof(line), "CANCEL\n");
                break;

            case TraceEntry::Kind::Reopen:
                std::snprintf(line, sizeof(line), "REOPEN\n");
                break;
        }
        out.append(line);

        if (withData && !entry.data.empty()) {
            out.append("        BF: ");
            appendHex(&out, entry.data);
            out.push_back('\n');
        }
    }
    return out;
}

}  // namespace g2710
