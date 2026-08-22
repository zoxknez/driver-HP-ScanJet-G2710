# G2710.Native - C ABI za .NET.
#
# Gradi se i kao STATICKA biblioteka (g2710_abi_obj) i kao DLL. Statička je za
# testove: tests/unit/abi_test.cpp vozi iste funkcije bez ucitavanja DLL-a, pa
# ScopedTestProvider i simulator zive u istom procesu.
#
# Bez toga bi test morao da radi LoadLibrary, a DLL bi nosio SVOJU kopiju
# TransportProvider singletona - ista zamka koja je zabelezena za wiaharness.

add_library(g2710_abi_obj STATIC G2710Abi.cpp)
target_link_libraries(g2710_abi_obj PUBLIC G2710::Core G2710::Sim)
target_include_directories(g2710_abi_obj PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/native/core)

# PUBLIC, ne PRIVATE: prekidac mora vaziti i za onoga ko ovu biblioteku koristi.
# Da vazi samo ovde, testovi bi trazili __imp_ simbole kojih u statickoj
# biblioteci nema - i to bi ispalo tek pri linkovanju.
target_compile_definitions(g2710_abi_obj PUBLIC G2710_ABI_STATIC)
add_library(G2710::Abi ALIAS g2710_abi_obj)

add_library(g2710_native SHARED G2710Abi.cpp G2710.Native.def)
target_link_libraries(g2710_native PRIVATE G2710::Core G2710::Sim)
target_include_directories(g2710_native PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/native/core)
target_compile_definitions(g2710_native PRIVATE G2710_ABI_BUILDING)
set_target_properties(g2710_native PROPERTIES
    OUTPUT_NAME "G2710.Native"
    PREFIX "")
