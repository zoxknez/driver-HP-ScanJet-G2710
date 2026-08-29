include(FetchContent)

set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)

FetchContent_Declare(googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.17.0
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(googletest)

# GoogleTest se ne gradi pod nasim /W4 /WX.
if(MSVC)
    foreach(target gtest gtest_main gmock gmock_main)
        if(TARGET ${target})
            target_compile_options(${target} PRIVATE /W0 /WX-)
        endif()
    endforeach()
endif()

include(GoogleTest)

# TWAIN harness: poziva isti DS_Entry kod bez instaliranog DSM-a. Time se
# meri state machine i Global arbitraza na x64 i x86, a DSM ostaje samo loader.
add_executable(g2710_twainharness_tests
    twainharness/twain_lifecycle_test.cpp
    ${CMAKE_SOURCE_DIR}/native/twain/TwainDataSource.cpp
)
target_include_directories(g2710_twainharness_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/native/twain
    ${CMAKE_SOURCE_DIR}/third_party/twain)
target_link_libraries(g2710_twainharness_tests PRIVATE G2710::Core G2710::Sim GTest::gtest_main)
target_compile_definitions(g2710_twainharness_tests PRIVATE
    G2710_TWAIN_ALLOW_UNQUALIFIED=1)
gtest_discover_tests(g2710_twainharness_tests)

# Produkcioni DS se proverava odvojeno, bez test-prekidaca. Simulator ne sme
# sam po sebi da ukloni H8 zabranu i pretvori neproverenu rezoluciju u ponudu.
add_executable(g2710_twain_release_policy_tests
    twainharness/twain_release_policy_test.cpp
    ${CMAKE_SOURCE_DIR}/native/twain/TwainDataSource.cpp
)
target_include_directories(g2710_twain_release_policy_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/native/twain
    ${CMAKE_SOURCE_DIR}/third_party/twain)
target_link_libraries(g2710_twain_release_policy_tests PRIVATE
    G2710::Core G2710::Sim GTest::gtest_main)
gtest_discover_tests(g2710_twain_release_policy_tests)

# Ne linkuje DSM: LoadLibrary nad konkretnim zvanicnim TWAINDSM.dll meri
# stvarni discovery ugovor i ostavlja build nezavisan od sistemske instalacije.
add_executable(g2710_twaindsm_smoke twaindsmharness/dsm_discovery_smoke.cpp)
target_include_directories(g2710_twaindsm_smoke PRIVATE ${CMAKE_SOURCE_DIR}/third_party/twain)

# --- profil: sve provere su compile-time, pa kompajliranje JESTE test -------
add_executable(g2710_profile_test unit/profile_generated_test.cpp)
target_link_libraries(g2710_profile_test PRIVATE G2710::Core)
add_test(NAME profile_generated COMMAND g2710_profile_test)

# --- jedinicni testovi jezgra ----------------------------------------------
add_executable(g2710_unit_tests
    unit/safety_level_test.cpp
    unit/result_test.cpp
    unit/device_arbiter_test.cpp
    unit/transport_provider_test.cpp
    unit/wia_clsid_test.cpp
    unit/wia_capabilities_test.cpp
    unit/wia_events_test.cpp
    unit/wia_item_context_test.cpp
    unit/qualification_test.cpp
    ${CMAKE_SOURCE_DIR}/native/cli/Qualification.cpp
    ${CMAKE_SOURCE_DIR}/native/wia/WiaCapabilities.cpp
    ${CMAKE_SOURCE_DIR}/native/wia/WiaEvents.cpp
    ${CMAKE_SOURCE_DIR}/native/wia/WiaItemContext.cpp
    unit/sim_transport_test.cpp
    unit/simulator_test.cpp
    unit/motion_guard_test.cpp
    unit/device_lifecycle_test.cpp
    unit/pnm_writer_test.cpp
    unit/adc_calibration_test.cpp
    unit/calibration_cache_test.cpp
    ${CMAKE_SOURCE_DIR}/native/cli/ImageOutput.cpp
)
target_link_libraries(g2710_unit_tests PRIVATE G2710::Core G2710::Sim GTest::gtest_main WiaGuid)
target_include_directories(g2710_unit_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/native/wia
    ${CMAKE_SOURCE_DIR}/native/cli)
target_compile_definitions(g2710_unit_tests PRIVATE
    G2710_INF_PATH="${CMAKE_SOURCE_DIR}/driver/g2710.inf"
    G2710_WIA_RC_PATH="${CMAKE_SOURCE_DIR}/native/wia/G2710Wia.rc"
    G2710_VERSION_PATH="${CMAKE_SOURCE_DIR}/VERSION")
gtest_discover_tests(g2710_unit_tests)

# --- golden register sequence testovi --------------------------------------
# Hvataju greske u BROJU i REDOSLEDU transfera, koje se inace vide tek na
# hardveru. Vidi tests/golden/register_sequence_test.cpp.
add_executable(g2710_golden_tests
    golden/register_sequence_test.cpp
    golden/dma_sequence_test.cpp
    golden/motor_sequence_test.cpp
    golden/sensor_timing_test.cpp
    golden/gpio_sequence_test.cpp
    golden/motor_steps_test.cpp
    golden/lamp_test.cpp
    golden/line_offset_test.cpp
    golden/pixel_format_test.cpp
    golden/resize_test.cpp
    golden/scan_registers_test.cpp
    golden/scan_planner_test.cpp
)
target_link_libraries(g2710_golden_tests PRIVATE G2710::Core G2710::Sim GTest::gtest_main)
gtest_discover_tests(g2710_golden_tests)

# --- integracioni testovi ---------------------------------------------------
# Kalibracija protiv simulatora koji NAMERNO gresi. Acceptance gate faze
# G2710-5 je ovde.
add_executable(g2710_integration_tests
    integration/calibration_test.cpp
    integration/scan_acquisition_test.cpp
    integration/scan_session_test.cpp
)
target_link_libraries(g2710_integration_tests PRIVATE G2710::Core G2710::Sim GTest::gtest_main)
gtest_discover_tests(g2710_integration_tests)

# --- WIA kvalifikacioni build ----------------------------------------------
# G2710_WIA_ALLOW_UNQUALIFIED je KOMPAJLERSKI prekidac, kao i BuildSafetyCeiling.
# Dva razlicita prekidaca u istom binarnom fajlu bila bi ODR krsenje, pa
# kvalifikaciona varijanta dobija svoju metu koja kompajlira SVOJU kopiju
# izvora.
#
# Bez ovoga bi u izdanju trinaest testova provere bilo preskoceno - ponuda je
# prazna, pa nema sta da se proverava. Ovako su obe grane pokrivene.
# Samo test mogucnosti - on jedini zavisi od prekidaca. Test dugmadi ne zavisi,
# pa bi ovde samo udvostrucio isto merenje.
add_executable(g2710_wia_qualification_test
    unit/wia_capabilities_test.cpp
    ${CMAKE_SOURCE_DIR}/native/wia/WiaCapabilities.cpp
)
target_link_libraries(g2710_wia_qualification_test PRIVATE G2710::Core GTest::gtest_main)
target_include_directories(g2710_wia_qualification_test PRIVATE
    ${CMAKE_SOURCE_DIR}/native/wia)
target_compile_definitions(g2710_wia_qualification_test PRIVATE
    G2710_WIA_ALLOW_UNQUALIFIED=1)
add_test(NAME wia_qualification COMMAND g2710_wia_qualification_test)

# --- C ABI ------------------------------------------------------------------
# Testira se STATICKA varijanta, u istom procesu: da se ucitavao DLL, on bi
# nosio SVOJU kopiju TransportProvider singletona i simulator iz test procesa
# ne bi imao efekta. Ista zamka kao kod wiaharness-a.
#
# Test stabilnosti cita .def i zaglavlje kao TEKST, pa mu se putanje
# prosledjuju - poredjenje izvezenog i deklarisanog nema smisla ako se oslanja
# na iste tipove koje meri.
add_executable(g2710_abi_tests
    unit/abi_test.cpp
    unit/abi_stability_test.cpp
)
target_link_libraries(g2710_abi_tests PRIVATE G2710::Abi GTest::gtest_main)
target_compile_definitions(g2710_abi_tests PRIVATE
    G2710_ABI_DEF_PATH="${CMAKE_SOURCE_DIR}/native/abi/G2710.Native.def"
    G2710_ABI_HEADER_PATH="${CMAKE_SOURCE_DIR}/native/abi/g2710_abi.h")
gtest_discover_tests(g2710_abi_tests)

# --- wiaharness: zivotni ciklus WIA drajvera --------------------------------
# Acceptance gate faze G2710-9.
#
# Izvori iz native/wia/ se KOMPAJLIRAJU OVDE, a DLL se ne ucitava. g2710_wia
# linkuje jezgro staticki, pa bi LoadLibrary + DllGetClassObject dao DLL-u
# SVOJU kopiju TransportProvider singletona - ScopedTestProvider iz test
# procesa ne bi imao efekta, a test bi i dalje prolazio, samo bi merio nesto
# drugo.
#
# G2710_WIA_ALLOW_UNQUALIFIED=1 jer bez toga ponuda je prazna, drvInitializeWia
# odbija na vratima, i nema zivotnog ciklusa koji bi se testirao. Grana
# "nema sta da se ponudi -> odbij" pokrivena je u izdanju, u wia_capabilities.
# Samo x64. native/wia/CMakeLists.txt preskace ceo minidriver na 32 bita, pa bi
# ovaj harness testirao komponentu koja se za x86 i ne isporucuje. Izmereno:
# x86 binarni fajl se ne moze ni pokrenuti - wiaservc ne postoji kao 32-bitni,
# pa se proces rusi sa STATUS_DLL_NOT_FOUND jos pre main().
if(CMAKE_SIZEOF_VOID_P EQUAL 8)

add_executable(g2710_wiaharness_tests
    wiaharness/wia_lifecycle_test.cpp
    ${CMAKE_SOURCE_DIR}/native/wia/G2710Usd.cpp
    ${CMAKE_SOURCE_DIR}/native/wia/G2710MiniDrv.cpp
    ${CMAKE_SOURCE_DIR}/native/wia/G2710Transfer.cpp
    ${CMAKE_SOURCE_DIR}/native/wia/WiaTransfer.cpp
    ${CMAKE_SOURCE_DIR}/native/wia/WiaItemContext.cpp
    ${CMAKE_SOURCE_DIR}/native/wia/WiaCapabilities.cpp
    ${CMAKE_SOURCE_DIR}/native/wia/WiaEvents.cpp
)
# sti.lib nosi IID_IStiDeviceControl; WiaGuid.lib nosi WIA_* i IID_IStiUSD.
# G2710Transfer.cpp se linkuje iako se ne moze pozvati bez servisa - bez njega
# G2710MiniDrv nema drvAcquireItemData i vtable je nepotpun.
target_link_libraries(g2710_wiaharness_tests
    PRIVATE G2710::Core G2710::Sim GTest::gtest_main Ole32 WiaGuid wiaservc sti)
target_include_directories(g2710_wiaharness_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/native/wia)
target_compile_definitions(g2710_wiaharness_tests PRIVATE
    G2710_WIA_ALLOW_UNQUALIFIED=1)
gtest_discover_tests(g2710_wiaharness_tests)

endif()

# --- arbitraza izmedju DVA PROCESA ------------------------------------------
# Acceptance gate faze G2710-4.
#
# unit/device_arbiter_test.cpp radi u jednom procesu, a bas to ne dokazuje
# nista: takav test prolazi i kada je brava obican std::mutex. `Global\`
# postoji zato sto proces nije granica koja nas zanima.
#
# Pravi Session 0 trazi Windows servis i ostaje H12; ovo hvata ono sto se
# offline moze uhvatiti - da je brava zaista medju-procesna.
add_executable(g2710_lock_holder arbiter/lock_holder.cpp)
target_link_libraries(g2710_lock_holder PRIVATE G2710::Core)

add_executable(g2710_arbiter_tests arbiter/arbiter_cross_process_test.cpp)
target_link_libraries(g2710_arbiter_tests PRIVATE G2710::Core GTest::gtest_main)
add_dependencies(g2710_arbiter_tests g2710_lock_holder)

# Putanja do pomocnog procesa se PROSLEDJUJE, ne pogadja. Generator expression
# je jedini nacin da radi i za jednokonfiguracione i za viseskonfiguracione
# generatore (Ninja vs Visual Studio).
target_compile_definitions(g2710_arbiter_tests PRIVATE
    G2710_LOCK_HOLDER_PATH="$<TARGET_FILE:g2710_lock_holder>")
gtest_discover_tests(g2710_arbiter_tests)

# --- plafon bezbednosti mora biti JEDNA vrednost po binarnom fajlu ----------
# Zato ovaj test ne linkuje G2710::Core (koji nosi PUBLIC plafon 5) nego
# kompajlira svoju kopiju izvora sa plafonom 1. Mesanje razlicitih plafona u
# istom binarnom fajlu bilo bi ODR krsenje i dalo bi lazno prolazan test.
add_executable(g2710_safety_ceiling1_test
    unit/safety_ceiling1_test.cpp
    ${CMAKE_SOURCE_DIR}/native/core/device/SafetyLevel.cpp
    ${CMAKE_SOURCE_DIR}/native/core/util/Error.cpp
)
target_include_directories(g2710_safety_ceiling1_test PRIVATE
    ${CMAKE_SOURCE_DIR}/native/core)
target_compile_definitions(g2710_safety_ceiling1_test PRIVATE
    G2710_BUILD_SAFETY_CEILING=1)
add_test(NAME safety_ceiling_1 COMMAND g2710_safety_ceiling1_test)

target_compile_definitions(g2710_twainharness_tests PRIVATE
    G2710_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
    G2710_VERSION_MINOR=${PROJECT_VERSION_MINOR})

target_compile_definitions(g2710_twain_release_policy_tests PRIVATE
    G2710_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
    G2710_VERSION_MINOR=${PROJECT_VERSION_MINOR})

target_compile_definitions(g2710_twaindsm_smoke PRIVATE
    G2710_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
    G2710_VERSION_MINOR=${PROJECT_VERSION_MINOR})
