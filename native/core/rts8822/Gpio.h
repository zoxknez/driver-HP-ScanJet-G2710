// GPIO i zakljucavanje registara.
//
// rts8822.c:764 SetLock, :792 Set_E950_Mode, :1070 RTS_Sensor_Type.

#pragma once

#include "../device/SafetyLevel.h"
#include "RegisterFile.h"

#include <chrono>
#include <cstdint>

namespace g2710::rts8822 {

// rts8822.c:1074 - vrednosti iz reference.
enum class SensorType : int {
    Cis = 0,
    Ccd = 1,
};

const char* toString(SensorType type) noexcept;

class Gpio {
public:
    Gpio(RegisterFile& registers, SafetyGate gate) : registers_(registers), gate_(gate) {}

    // rts8822.c:764 SetLock - bit 2 registra kLock.
    Status setLock(bool enabled);

    // rts8822.c:792 Set_E950_Mode - bit 6 registra kGpio0.
    // Cita REC pa upisuje REC, za razliku od Motor_Change koji upisuje bajt.
    Status setE950Mode(bool enabled);

    // rts8822.c:1070 RTS_Sensor_Type.
    //
    // Sekvenca CUVA pa VRACA originalne GPIO vrednosti - detekcija ne sme
    // ostaviti uredjaj u drugom stanju nego sto ga je zatekla. Izmedju
    // postavljanja i citanja ide pauza od 200 ms.
    //
    // `settleTime` je izlozen radi testova; u radu ostaje podrazumevanih 200 ms.
    Result<SensorType> detectSensorType(
        std::chrono::milliseconds settleTime = std::chrono::milliseconds{200});

private:
    RegisterFile& registers_;
    SafetyGate gate_;
};

}  // namespace g2710::rts8822
