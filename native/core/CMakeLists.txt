add_library(g2710_core STATIC
    util/Error.cpp

    device/SafetyLevel.cpp
    device/DeviceArbiter.cpp

    transport/TransportProvider.cpp
    transport/UsbScanTransport.cpp
)

add_library(G2710::Core ALIAS g2710_core)

target_include_directories(g2710_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/device   # G2710Profile.generated.h
)

# BuildSafetyCeiling je PUBLIC: svaki potrosac jezgra mora videti isti plafon,
# inace bi CLI mogao misliti da sme nesto sto jezgro odbija.
target_compile_definitions(g2710_core PUBLIC
    G2710_BUILD_SAFETY_CEILING=${G2710_BUILD_SAFETY_CEILING}
)

if(G2710_ENABLE_WINUSB_LAB_TRANSPORT)
    target_compile_definitions(g2710_core PUBLIC G2710_WINUSB_LAB_TRANSPORT=1)
    message(WARNING
        "WinUSB lab transport je ukljucen. Zahteva ZASEBAN INF i PnP rebind "
        "uredjaja - nije runtime fallback i ne sme ici u release.")
endif()

target_link_libraries(g2710_core PUBLIC
    Advapi32   # ConvertStringSecurityDescriptorToSecurityDescriptorW
)

target_compile_features(g2710_core PUBLIC cxx_std_20)
