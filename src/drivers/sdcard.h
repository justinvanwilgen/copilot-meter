#pragma once

#include <SD.h>

// Mounts the microSD card; afterwards use the global `SD` filesystem object.
bool sdcardInit();
