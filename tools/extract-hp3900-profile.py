#!/usr/bin/env python
"""
Ekstraktuje HP ScanJet G2710 device profile iz hp3900 reference izvora u
generisani C++ header.

Izvor istine: third_party/hp3900-reference/  (GPL-2.0, Jonathan Bravo Lopez)
Izlaz:        native/core/device/G2710Profile.generated.h

G2710 (HPG2710 = 0x07) je u svakoj konfiguracionoj tabeli identican HP3800, i
svaka switch(dev_model) grana dispecuje na hp3800_* familiju. Zato:
  - tabele koje kljucaju na uredjaj  -> filtriramo red za HPG2710
  - hp3800_* tabele                  -> uzimamo u celosti (vec su G2710-specificne)

Generisani header je VERNA TRANSKRIPCIJA. Interpretacija (lookup pravila,
snapping rezolucije, default vrednosti) pripada engine kodu u native/core/.

Pokretanje:  python tools/extract-hp3900-profile.py
"""

from __future__ import annotations

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from hp3900_parse import (  # noqa: E402
    REF, ROOT, OUT,
    decode_motor_curves, find_function_body, find_table_initializer, flat,
    parse_braces, parse_defines, parse_enum, parse_switch_map, rows_block,
    strip_comments,
)

UPSTREAM_COMMIT = "951e8a5a"

HEADER = """// GENERATED FILE - DO NOT EDIT.
//
// Generisao: tools/extract-hp3900-profile.py
// Izvor:     third_party/hp3900-reference/ @ {commit}
//            hp3900 SANE backend, Copyright (C) 2005-2008 Jonathan Bravo Lopez
//            GPL-2.0-or-later. Vidi NOTICE-hp3900.md.
//
// Verna transkripcija G2710 konfiguracionih tabela. Interpretacija (lookup
// pravila, snapping rezolucije, default vrednosti) pripada engine kodu u
// native/core/, ne ovom fajlu.
//
// G2710 == HP3800 u svakoj tabeli; hp3800_* tabele su vec G2710-specificne.

#pragma once

#include <cstdint>

namespace g2710::profile {{

// ---------------------------------------------------------------- identitet
inline constexpr std::uint16_t kUsbVendorId  = 0x{vid:04X};
inline constexpr std::uint16_t kUsbProductId = 0x{pid:04X};
inline constexpr int kDeviceModelId  = {model};    // HPG2710
inline constexpr int kChipsetModelId = {chipset};    // RTS8822BL_03A
inline constexpr const char* kChipsetName = "RTS8822BL-03A";

// ------------------------------------------------------------ register bank
inline constexpr int kRegisterBankBase   = 0xE800;
inline constexpr int kRegisterBankLength = {rtlen};  // RT_BUFFER_LEN = 0x71a

// ----------------------------------------------------------------- transport
// Vidi docs/PROTOCOL-RTS8822.md. wIndex je selektor komande, ne adresa.
inline constexpr std::uint8_t kVendorRequest     = 0x04;
inline constexpr std::uint8_t kBulkInEndpoint    = 0x81;
inline constexpr std::uint8_t kBulkOutEndpoint   = 0x02;
inline constexpr int          kTransferTimeoutMs = 1000;

inline constexpr int kIndexRegisterWrite  = 0x0000;
inline constexpr int kIndexRegisterRead   = 0x0100;
inline constexpr int kIndexEeprom         = 0x0200;
inline constexpr int kIndexDmaEnableRead  = 0x0400;
inline constexpr int kIndexDmaEnableWrite = 0x0401;
inline constexpr int kIndexDmaCancel      = 0x0600;
inline constexpr int kIndexDmaOpType      = 0x0800;
inline constexpr int kIndexChipsetReset   = 0x0801;
"""

FOOTER = """
}  // namespace g2710::profile
"""


def emit_struct(name: str, fields: list) -> str:
    """Polje moze nositi trailing `// komentar`; tacka-zarez ide PRE njega."""
    lines = []
    for f in fields:
        decl, sep, comment = f.partition("//")
        decl = decl.rstrip()
        lines.append("    " + decl + ";" + ("  //" + comment if sep else ""))
    return "\nstruct " + name + " {\n" + "\n".join(lines) + "\n};\n"


def main() -> int:
    types_src = strip_comments((REF / "hp3900_types.c").read_text(errors="replace"))
    cfg = strip_comments((REF / "hp3900_config.c").read_text(errors="replace"))
    syms = parse_defines(types_src)
    g2710 = syms["HPG2710"]

    def table(fn: str, arr: str):
        return parse_braces(find_table_initializer(find_function_body(cfg, fn), arr), syms)

    def dev_row(fn: str, arr: str):
        """Red za HPG2710 iz tabele koja kljuca na uredjaj."""
        for r in table(fn, arr):
            if r and r[0] == g2710:
                return r[1] if len(r) == 2 else r[1:]
        raise KeyError("HPG2710 red nije nadjen u " + fn)

    vid = pid = None
    for r in table("cfg_device_get", "myreg"):
        if r[2] == g2710:
            vid, pid = r[0], r[1]
    if vid is None:
        raise KeyError("G2710 USB id nije nadjen")
    chipset = dev_row("cfg_chipset_model_get", "myreg")

    out = [HEADER.format(commit=UPSTREAM_COMMIT, vid=vid, pid=pid, model=g2710,
                         chipset=chipset, rtlen=syms["RT_BUFFER_LEN"])]
    w = out.append

    # -------------------------------------------------- device-keyed tabele
    w("\n// ------------------------------------------------------------------ senzor")
    w(emit_struct("SensorConfig", [
        "int type", "int name", "int resolution",
        "int channelColor[3]", "int channelGray[2]", "int rgbOrder[3]",
        "int lineDistance      // u jedinicama 2400 dpi",
        "int evenOddDistance   // u jedinicama 2400 dpi"]))
    w("inline constexpr SensorConfig kSensor " + flat(dev_row("cfg_sensor_get", "myreg")) + ";\n")

    w("\n// ------------------------------------------------------------------- motor")
    w(emit_struct("MotorConfig", [
        "int type", "int resolution", "int pwmFrequency", "int baseSpeedPps",
        "int baseSpeedMotorMove", "int highSpeedMotorMove",
        "int parkHomeMotorMove", "int changeMotorCurrent"]))
    w("inline constexpr MotorConfig kMotor " + flat(dev_row("cfg_motor_get", "myreg")) + ";\n")

    w("\n// ----------------------------------------------------------------- dugmad")
    w(emit_struct("ButtonConfig", ["int count", "int mask[6]"]))
    w("inline constexpr ButtonConfig kButtons " + flat(dev_row("cfg_buttons_get", "myreg")) + ";\n")

    w("\n// ------------------------------------------------ spectrum clock generator")
    w(emit_struct("SscgConfig", ["int enable", "int mode", "int clock"]))
    w("inline constexpr SscgConfig kSscg " + flat(dev_row("cfg_sscg_get", "myreg")) + ";\n")

    w("\n// ------------------------------------------------ ogranicenja povrsine (mm)")
    w(emit_struct("Coords", ["int left", "int width", "int top", "int height"]))
    w(emit_struct("Constraints", ["Coords reflective", "Coords negative", "Coords slide"]))
    w("inline constexpr Constraints kConstraints " +
      flat(dev_row("cfg_constrains_get", "reg")) + ";\n")

    w("\n// -------------------------------------------------- auto reference position")
    w(emit_struct("AutoRef", ["int type", "int offsetX  // bazirano na 2400 dpi",
                              "int offsetY  // bazirano na 2400 dpi",
                              "int resolution", "int externBoundary"]))
    w("inline constexpr AutoRef kAutoRef " + flat(dev_row("cfg_autoref_get", "myreg")) + ";\n")

    # ------------------------------------------------------- hp3800 tabele
    w("\n// ----------------------------------------------------- referentni naponi CCD")
    w(emit_struct("RefVoltages", ["int usb", "int sensor",
                                  "int values[3]  // vrts, vrms, vrbs"]))
    w("inline constexpr RefVoltages kRefVoltages[] = {\n" +
      rows_block(table("hp3800_refvoltages", "myreg")) + "\n};\n")

    w("\n// ------------------------------------------ offset kalibracija: left / width")
    w(emit_struct("OffsetPair", ["int left", "int width"]))
    w(emit_struct("OffsetRow", ["int resolution", "OffsetPair reflective",
                                "OffsetPair transparent", "OffsetPair negative"]))
    w("inline constexpr OffsetRow kOffsets[] = {\n" +
      rows_block([[r[0]] + r[1] for r in table("hp3800_offset", "myreg")]) + "\n};\n")

    w("\n// -------------------------------------------------------- efektivni pikseli")
    w(emit_struct("EffectivePixelRow", ["int resolution", "int pixel"]))
    w("inline constexpr int kEffectivePixelDefault = 230;")
    w("inline constexpr EffectivePixelRow kEffectivePixels[] = {\n" +
      rows_block(table("hp3800_effectivepixel", "reg")) + "\n};\n")

    w("\n// -------------------------------------------------------- ADC gain / offset")
    w(emit_struct("GainOffset", [
        "int evenOffset1[3]", "int evenOffset2[3]",
        "int oddOffset1[3]", "int oddOffset2[3]",
        "int pag[3]", "int vgag1[3]", "int vgag2[3]"]))
    w(emit_struct("GainOffsetRow", ["int usb", "GainOffset values"]))
    w("inline constexpr GainOffsetRow kGainOffsets[] = {\n" +
      rows_block(table("hp3800_gainoffset", "reg")) + "\n};\n")

    w("\n// ---------------------------------------------- kriterijum stabilnosti lampe")
    w(emit_struct("CheckStable", ["double diff", "int intervalMs", "long totalTimeMs"]))
    w(emit_struct("CheckStableRow", ["int lamp", "CheckStable values"]))
    w("inline constexpr CheckStableRow kCheckStable[] = {\n" +
      rows_block(table("hp3800_checkstable", "reg")) + "\n};\n")

    w("\n// ------------------------------------------------------------ fiksni PWM lampe")
    w(emit_struct("FixedPwmRow", ["int usb", "int pwm[3]  // ST_NORMAL, ST_TA, ST_NEG"]))
    w("inline constexpr int kFixedPwmDefault = 0x16;")
    w("inline constexpr FixedPwmRow kFixedPwm[] = {\n" +
      rows_block(table("hp3800_fixedpwm", "reg")) + "\n};\n")

    w("\n// ---------------------------------------------------------------- SER / LER")
    w(emit_struct("VrefRow", ["int resolution", "int ser", "int ler"]))
    w("inline constexpr VrefRow kVrefs[] = {\n" +
      rows_block([[r[0]] + r[1] for r in table("hp3800_vrefs", "reg")]) + "\n};\n")

    w("\n// -------------------------------------------------------------- scan modovi")
    w(emit_struct("ScanMode", [
        "int scanType", "int colorMode", "int resolution",
        "int timing", "int motorCurve", "int sampleRate", "int systemClock",
        "int ctpc", "int motorBackStep", "int scanMotorStepType",
        "int dummyLine", "int expt[3]", "int mexpt[3]",
        "int motorPlus", "int multiExposureFor16BitMode",
        "int multiExposureForFullSpeed", "int multiExposure",
        "int mri", "int msi", "int mmtir", "int mmtirh", "int skipLineCount"]))
    w(emit_struct("ScanModeRow", ["int usb", "ScanMode mode"]))
    scan_modes = table("hp3800_scanmodes", "reg")
    w("inline constexpr ScanModeRow kScanModes[] = {\n" + rows_block(scan_modes) + "\n};\n")

    w("\n// --------------------------------------------------- CCD timing (Toshiba T2905)")
    w("// cphp1 / cphp2 su 36-bitne maske faza clock-a, zato double (kao u referenci).")
    w(emit_struct("Cph", ["double p1", "double p2", "int ps", "int ge", "int go"]))
    w(emit_struct("Timing", [
        "int sensorResolution", "int cnpp", "int cvtrp[3]",
        "int cvtrw", "int cvtrfpw", "int cvtrbpw", "Cph cph[6]",
        "int cphbp2s", "int cphbp2e", "int clamps", "int clampe",
        "int cdss[2]", "int cdsc[2]", "int cdscs[2]",
        "double adcclkp[2]", "int adcclkp2e"]))
    timings = table("hp3800_timing_get", "data")
    w("inline constexpr Timing kTimings[] = {\n" + rows_block(timings) + "\n};\n")

    w("\n// ---------------------------------------------------------- profili kretanja")
    w(emit_struct("MotorMove", ["int systemClock", "int ctpc",
                                "int scanMotorStepType", "int motorCurve"]))
    w("inline constexpr MotorMove kMotorMoves[] = {\n" +
      rows_block(table("hp3800_motormove", "mv")) + "\n};\n")

    w("\n// -------------------------------------------------------------- shading cut")
    w(emit_struct("ShadingCutRow", ["int resolution", "int reflective[3]",
                                    "int transparent[3]", "int negative[3]"]))
    w("inline constexpr ShadingCutRow kShadingCuts[] = {\n" +
      rows_block(table("hp3800_shading_cut", "cuts")) + "\n};\n")

    w("\n// ---------------------------------------------------------------- white refs")
    w("// NAPOMENA: za flatbed (ST_NORMAL) referenca IGNORISE ovu tabelu i vraca")
    w("// fiksno 248 / 250 / 248. Tabela vazi samo za ST_TA i ST_NEG.")
    w("inline constexpr int kWhiteRefReflective[3] = {248, 250, 248};")
    w(emit_struct("WhiteRefRow", ["int resolution", "int transparent[3]", "int negative[3]"]))
    w("inline constexpr WhiteRefRow kWhiteRefs[] = {\n" +
      rows_block(table("hp3800_wrefs", "wrefs")) + "\n};\n")

    # ------------------------------------------------------------ motorne krive
    steps = parse_braces(find_table_initializer(find_function_body(cfg, "hp3800_motor"),
                                                "steps"), syms)[0]
    curves = decode_motor_curves(steps)
    w("\n// ------------------------------------------------------------- motorne krive")
    w("// Referenca ih drzi kao flat int stream sa terminatorima (0 / -2 / -1).")
    w("// Ovde su dekodovane u strukture; sadrzaj je identican.")
    w(emit_struct("MotorCurveSegment", [
        "int type            // ACC_CURVE = 0, DEC_CURVE = 1",
        "int name            // CRV_NORMALSCAN / PARKHOME / SMEARING / BUFFERFULL",
        "const int* values", "int count"]))
    w(emit_struct("MotorCurve", [
        "int mri", "int msi", "int skipLineCount", "int motorBackStep",
        "MotorCurveSegment segments[7]"]))
    for ci, c in enumerate(curves):
        for si, s in enumerate(c["segments"]):
            w("inline constexpr int kMotorCurve%dSeg%d[] = %s;" % (ci, si, flat(s["values"])))
    w("\ninline constexpr MotorCurve kMotorCurves[] = {")
    for ci, c in enumerate(curves):
        segs = ", ".join("{%d, %d, kMotorCurve%dSeg%d, %d}" %
                         (s["type"], s["name"], ci, si, len(s["values"]))
                         for si, s in enumerate(c["segments"]))
        w("    {%d, %d, %d, %d, {%s}}," % (c["mri"], c["msi"], c["skiplinecount"],
                                           c["motorbackstep"], segs))
    w("};\n")

    # ------------------------------------------------------ kalibracioni parametri
    opts = parse_enum(cfg, "WSTRIPXPOS")
    w("\n// --------------------------------------------------- kalibracioni parametri")
    w("// Referenca ih drzi kao switch(option) mapiranja. Redosled enuma je ocuvan.")
    w("enum class CalibOption : int {")
    for i, o in enumerate(opts):
        w("    " + o + " = " + str(i) + ",")
    w("};\n")
    w(emit_struct("CalibEntry", ["CalibOption option", "int value"]))

    calib_counts = {}
    for cname, fn in [("kCalibReflective", "hp3800_calibreflective"),
                      ("kCalibTransparent", "hp3800_calibtransparent"),
                      ("kCalibNegative", "hp3800_calibnegative"),
                      ("kScanParams", "srt_hp3800_scanparam_get"),
                      ("kPlatformParams", "srt_hp3800_platform_get")]:
        m = parse_switch_map(find_function_body(cfg, fn), syms)
        calib_counts[cname] = len(m)
        w("inline constexpr CalibEntry " + cname + "[] = {")
        for k, v in m.items():
            w("    {CalibOption::" + k + ", " + str(v) + "},")
        w("};\n")

    out.append(FOOTER)
    OUT.parent.mkdir(parents=True, exist_ok=True)
    OUT.write_text("\n".join(out), encoding="utf-8")

    print("upisano: " + str(OUT.relative_to(ROOT)))
    print("  scan modova     : %d" % len(scan_modes))
    print("  timing profila  : %d" % len(timings))
    print("  motornih krivih : %d" % len(curves))
    print("  calib opcija    : %d" % len(opts))
    for k, v in calib_counts.items():
        print("    %-20s %d" % (k, v))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
