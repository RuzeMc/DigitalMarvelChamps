#pragma once
#include "CoreMinimal.h"

#ifndef AS_CAN_GENERATE_JIT
#define AS_CAN_GENERATE_JIT (PLATFORM_WINDOWS || PLATFORM_LINUX)
#endif

#if WITH_EDITOR
#define AS_SKIP_JITTED_CODE
#endif

#ifndef AS_JIT_VERIFY_PROPERTY_OFFSETS
#define AS_JIT_VERIFY_PROPERTY_OFFSETS (!UE_BUILD_SHIPPING && !UE_BUILD_TEST)
#endif
