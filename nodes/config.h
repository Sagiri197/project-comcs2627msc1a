#pragma once

#include "config_types.h"

#ifndef NODE_PROFILE
#define NODE_PROFILE 1
#endif

#if NODE_PROFILE == 1
#include "configs/esp32_001.h"
#elif NODE_PROFILE == 2
#include "configs/esp32_002.h"
#elif NODE_PROFILE == 3
#include "configs/esp32_003.h"
#else
#error "NODE_PROFILE must be 1, 2, or 3"
#endif