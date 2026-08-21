# WIA 2.0 minidriver.
#
# Sloj odluka (WiaCapabilities) je odvojen od COM ljuske i gradi se i kao deo
# testova - vidi tests/CMakeLists.txt. Tako se ono sto se moze pogresiti
# testira bez ijednog COM objekta.
#
# Samo x64 - WIA minidriver se na x64 Windowsu ucitava kao 64-bitna
# komponenta. TWAIN je taj koji ide i x86 i x64.

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(STATUS "G2710.Wia se preskace: WIA minidriver je samo x64")
    return()
endif()

add_library(g2710_wia SHARED
    G2710Wia.cpp
    G2710Usd.cpp
    WiaCapabilities.cpp
    WiaEvents.cpp
    G2710Wia.def
)

set_target_properties(g2710_wia PROPERTIES
    OUTPUT_NAME "G2710.Wia"
    PREFIX ""
)

# WiaGuid.lib nosi definicije WIA_EVENT_* i IID_IStiUSD. Bez nje se ti simboli
# ne razresavaju - deklaracije u zaglavljima su samo `extern const GUID`.
target_link_libraries(g2710_wia PRIVATE G2710::Core Ole32 WiaGuid)

# Paket koji ide u potpisivanje. Skripte u driver/sign/ ocekuju INF i DLL na
# istom mestu jer Inf2Cat radi nad direktorijumom, ne nad pojedinacnim fajlom.
set(G2710_PACKAGE_DIR "${CMAKE_BINARY_DIR}/package")

add_custom_command(TARGET g2710_wia POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${G2710_PACKAGE_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:g2710_wia>" "${G2710_PACKAGE_DIR}/"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_SOURCE_DIR}/driver/g2710.inf" "${G2710_PACKAGE_DIR}/"
    COMMENT "Sastavljam driver paket u ${G2710_PACKAGE_DIR}"
    VERBATIM
)
