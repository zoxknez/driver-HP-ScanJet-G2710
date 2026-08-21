add_library(g2710_core STATIC
    util/Clock.cpp
    util/Error.cpp

    calib/CalibrationConfig.cpp
    calib/ShadingCalibration.cpp

    image/LineOffset.cpp
    image/PixelFormat.cpp
    image/Resize.cpp

    device/DeviceArbiter.cpp
    device/DeviceState.cpp
    device/G2710Device.cpp
    device/MotionGuard.cpp
    device/SafetyLevel.cpp

    transport/TraceRecorder.cpp
    transport/TransportProvider.cpp
    transport/UsbScanTransport.cpp

    scan/Capabilities.cpp
    scan/ScanPlanner.cpp

    rts8822/Dma.cpp
    rts8822/Gpio.cpp
    rts8822/Lamp.cpp
    rts8822/Motor.cpp
    rts8822/MotorSteps.cpp
    rts8822/RegisterBank.cpp
    rts8822/RegisterFile.cpp
    rts8822/ScanRegisters.cpp
    rts8822/SensorTiming.cpp
    rts8822/Rts8822.cpp
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
