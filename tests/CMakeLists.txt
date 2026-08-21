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
    G2710_INF_PATH="${CMAKE_SOURCE_DIR}/driver/g2710.inf")
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
