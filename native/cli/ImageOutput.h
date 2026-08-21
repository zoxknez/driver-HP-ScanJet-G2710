// Upis slike u PNM, dijagnosticki format CLI-ja.
//
// PNM je izabran namerno: zaglavlje je citljivo golim okom, nema kompresije
// koja bi sakrila gresku, i otvara ga svaki alat. Kada H faza vrati sliku sa
// prijateljevog uredjaja, bajt na 1000-om mestu mora da znaci tacno ono sto
// pise - PNG bi to sakrio iza deflate-a.
//
// Aplikacija i WIA koriste prave formate; ovo je za dijagnostiku.
//
//   P4  lineart, jedan bit po tacki
//   P5  sivo, 8 ili 16 bita
//   P6  boja, 8 ili 16 bita po kanalu
//
// PNM sa 16 bita je BIG-endian po specifikaciji, a nas cevovad radi
// little-endian, pa se pri upisu obrce. Bez toga svaka 16-bitna slika izgleda
// kao sum.

#pragma once

#include "image/PixelFormat.h"
#include "util/Result.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <span>
#include <string>

namespace g2710::cli {

class PnmWriter {
public:
    ~PnmWriter();

    PnmWriter(const PnmWriter&) = delete;
    PnmWriter& operator=(const PnmWriter&) = delete;

    // Otvori fajl i upisi zaglavlje.
    static Result<PnmWriter*> create(const std::string& path, image::ColorMode mode, int depth,
                                     int width, int height);

    // Upisi jedan red onako kako ga sesija daje.
    Status writeLine(std::span<const std::uint8_t> line);

    Status close();

    const std::string& path() const noexcept { return path_; }
    const char* formatName() const noexcept;
    int linesWritten() const noexcept { return linesWritten_; }

private:
    PnmWriter(std::FILE* file, std::string path, image::ColorMode mode, int depth, int width,
              int height);

    std::FILE* file_ = nullptr;
    std::string path_;
    image::ColorMode mode_ = image::ColorMode::Color;
    int depth_ = 8;
    int width_ = 0;
    int height_ = 0;
    int linesWritten_ = 0;
    std::string swapBuffer_;
};

// Preporucena ekstenzija za dati rezim.
const char* pnmExtension(image::ColorMode mode) noexcept;

}  // namespace g2710::cli
