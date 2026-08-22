#pragma once

// Zvanicni twain.h za MSVC namerno ne ukljucuje Windows.h, ali koristi HANDLE,
// LPVOID i UINT_PTR iz njega. Ukljucivanje ovde drzi taj preduslov na granici.
#include <Windows.h>
#include "twain.h"

// Javna samo za harness: isti kod koji izvozi DS_Entry se poziva bez DSM-a.
TW_UINT16 TW_CALLINGSTYLE G2710TwainEntry(pTW_IDENTITY origin,
                                          pTW_IDENTITY destination,
                                          TW_UINT32 dataGroup,
                                          TW_UINT16 dataArgumentType,
                                          TW_UINT16 message,
                                          TW_MEMREF data);
