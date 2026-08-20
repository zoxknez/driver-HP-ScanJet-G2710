cmake_minimum_required(VERSION 3.25)

project(G2710
    VERSION 0.1.0
    DESCRIPTION "HP ScanJet G2710 native Windows 10/11 driver"
    LANGUAGES CXX)

# ---------------------------------------------------------------- opcije ----

# BuildSafetyCeiling je NEPROMENLJIV plafon ugradjen u binarni fajl. Efektivni
# nivo je min(ceiling, requested); CLI/config ga NIKADA ne mogu podici iznad
# ovoga. Qualification paketi se grade sa nizim plafonom - prvi koji ide
# prijatelju ima 1, sto znaci da motor path nije ni kompajliran.
#
#   1 enumeracija, descriptor, read-only registri
#   2 + safe konfiguracioni write, lampe
#   3 + motor / home
#   4 + CCD akvizicija
#   5 + pun scan
set(G2710_BUILD_SAFETY_CEILING "5" CACHE STRING
    "Nepromenljivi plafon bezbednosnog nivoa ugradjen u build (1-5)")
set_property(CACHE G2710_BUILD_SAFETY_CEILING PROPERTY STRINGS 1 2 3 4 5)

if(NOT G2710_BUILD_SAFETY_CEILING MATCHES "^[1-5]$")
    message(FATAL_ERROR
        "G2710_BUILD_SAFETY_CEILING mora biti 1-5, dobijeno: ${G2710_BUILD_SAFETY_CEILING}")
endif()

option(G2710_BUILD_TESTS "Gradi testove" ON)

# WinUsbTransport je LAB / CONTINGENCY ONLY i zahteva zaseban INF plus PnP
# rebind uredjaja. Nije runtime fallback i ne ulazi u release build.
option(G2710_ENABLE_WINUSB_LAB_TRANSPORT
       "Ukljuci WinUSB lab transport (zahteva zaseban INF i PnP rebind)" OFF)

# ------------------------------------------------------------- podesavanja ---

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT WIN32)
    message(FATAL_ERROR "G2710 se gradi samo za Windows.")
endif()

if(MSVC)
    add_compile_options(/W4 /WX /permissive- /EHsc /utf-8 /Zc:__cplusplus)
    add_compile_definitions(
        UNICODE _UNICODE
        WIN32_LEAN_AND_MEAN
        NOMINMAX)
endif()

message(STATUS "G2710 build safety ceiling: ${G2710_BUILD_SAFETY_CEILING}")

add_subdirectory(native/core)

if(G2710_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
