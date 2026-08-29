cmake_minimum_required(VERSION 3.25)

# Verzija stoji na JEDNOM mestu.
#
# Ranije je bila prepisana ovde, a nezavisno i u driver/g2710.inf. Korenski
# VERSION je govorio 0.1.0, INF je Windows-u prijavljivao 1.0.0.0. Broj koji
# Device Manager prikazuje bio je izmisljen - a to je bas onaj broj koji
# prijatelj procita kada ga pitamo koji build ima.
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" G2710_VERSION_RAW)
string(STRIP "${G2710_VERSION_RAW}" G2710_VERSION_RAW)
if(NOT G2710_VERSION_RAW MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+$")
    message(FATAL_ERROR "VERSION nije oblika X.Y.Z: '${G2710_VERSION_RAW}'")
endif()

project(G2710
    VERSION ${G2710_VERSION_RAW}
    DESCRIPTION "HP ScanJet G2710 native Windows 10/11 driver"
    LANGUAGES CXX)

enable_language(RC)

# Verzijski resurs za isporucene binarne fajlove.
#
# Poziva se za svaki fajl koji ide korisniku. Bez njega Windows u osobinama
# fajla prikazuje prazno, pa se sa tudjeg racunara ne moze utvrditi koji je
# build tamo.
function(g2710_add_version_resource target description filename filetype)
    set(G2710_VERSION_MAJOR ${PROJECT_VERSION_MAJOR})
    set(G2710_VERSION_MINOR ${PROJECT_VERSION_MINOR})
    set(G2710_VERSION_PATCH ${PROJECT_VERSION_PATCH})
    set(G2710_VERSION_STRING "${PROJECT_VERSION}")
    set(G2710_VERSION_DESCRIPTION "${description}")
    set(G2710_VERSION_FILENAME "${filename}")
    set(G2710_VERSION_INTERNAL "${target}")
    set(G2710_VERSION_FILETYPE "${filetype}")

    set(generated "${CMAKE_CURRENT_BINARY_DIR}/${target}.version.rc")
    configure_file("${CMAKE_SOURCE_DIR}/native/version.rc.in" "${generated}" @ONLY)
    target_sources(${target} PRIVATE "${generated}")
endfunction()

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
add_subdirectory(native/sim)
add_subdirectory(native/abi)
add_subdirectory(native/cli)
add_subdirectory(native/wia)
add_subdirectory(native/twain)

if(G2710_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
