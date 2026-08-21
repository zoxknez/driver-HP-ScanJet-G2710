#include "ImageOutput.h"

#include <algorithm>
#include <memory>

namespace g2710::cli {

const char* pnmExtension(image::ColorMode mode) noexcept {
    switch (mode) {
        case image::ColorMode::Color:   return "ppm";
        case image::ColorMode::Gray:    return "pgm";
        case image::ColorMode::Lineart: return "pbm";
    }
    return "pnm";
}

PnmWriter::PnmWriter(std::FILE* file, std::string path, image::ColorMode mode, int depth,
                     int width, int height)
    : file_(file),
      path_(std::move(path)),
      mode_(mode),
      depth_(depth),
      width_(width),
      height_(height) {}

PnmWriter::~PnmWriter() {
    if (file_ != nullptr) {
        std::fclose(file_);
    }
}

const char* PnmWriter::formatName() const noexcept {
    switch (mode_) {
        case image::ColorMode::Lineart: return "P4 (lineart)";
        case image::ColorMode::Gray:    return depth_ == 16 ? "P5 (sivo, 16 bita)" : "P5 (sivo)";
        case image::ColorMode::Color:   return depth_ == 16 ? "P6 (boja, 48 bita)" : "P6 (boja)";
    }
    return "?";
}

Result<PnmWriter*> PnmWriter::create(const std::string& path, image::ColorMode mode, int depth,
                                     int width, int height) {
    if (width <= 0 || height <= 0) {
        return fail(ErrorCode::InvalidArgument, "PnmWriter: prazna slika");
    }

    std::FILE* file = nullptr;
    // MSVC odbija fopen pod /WX; fopen_s je isti poziv sa provernim kodom.
    if (const errno_t error = fopen_s(&file, path.c_str(), "wb"); error != 0 || file == nullptr) {
        return fail(ErrorCode::Internal, "PnmWriter: ne mogu da otvorim fajl",
                    static_cast<std::uint32_t>(error));
    }

    auto writer = std::unique_ptr<PnmWriter>(new PnmWriter(file, path, mode, depth, width, height));

    int written = 0;
    if (mode == image::ColorMode::Lineart) {
        written = std::fprintf(file, "P4\n%d %d\n", width, height);
    } else {
        const int maxValue = depth == 16 ? 65535 : 255;
        written = std::fprintf(file, "%s\n%d %d\n%d\n",
                               mode == image::ColorMode::Color ? "P6" : "P5", width, height,
                               maxValue);
    }
    if (written <= 0) {
        return fail(ErrorCode::Internal, "PnmWriter: upis zaglavlja nije uspeo");
    }

    return writer.release();
}

Status PnmWriter::writeLine(std::span<const std::uint8_t> line) {
    if (file_ == nullptr) {
        return fail(ErrorCode::InvalidState, "PnmWriter: fajl je zatvoren");
    }
    if (line.empty()) {
        return fail(ErrorCode::InvalidArgument, "PnmWriter: prazan red");
    }

    const void* data = line.data();
    std::size_t count = line.size();

    if (depth_ == 16 && mode_ != image::ColorMode::Lineart) {
        // PNM je big-endian; nas cevovod je little-endian.
        swapBuffer_.resize(count);
        for (std::size_t i = 0; i + 1 < count; i += 2) {
            swapBuffer_[i] = static_cast<char>(line[i + 1]);
            swapBuffer_[i + 1] = static_cast<char>(line[i]);
        }
        data = swapBuffer_.data();
    }

    if (std::fwrite(data, 1, count, file_) != count) {
        return fail(ErrorCode::Internal, "PnmWriter: upis reda nije uspeo");
    }
    ++linesWritten_;
    return ok();
}

Status PnmWriter::close() {
    if (file_ == nullptr) {
        return ok();
    }
    const int result = std::fclose(file_);
    file_ = nullptr;
    if (result != 0) {
        return fail(ErrorCode::Internal, "PnmWriter: zatvaranje nije uspelo");
    }
    if (linesWritten_ != height_) {
        // Zaglavlje vec tvrdi koliko redova ima. Ako ih je manje, fajl je
        // pokvaren i to se mora reci odmah, a ne ostaviti citaocu da otkrije.
        return fail(ErrorCode::ShortTransfer, "PnmWriter: upisano manje redova nego sto zaglavlje tvrdi");
    }
    return ok();
}

}  // namespace g2710::cli
