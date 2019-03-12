#include "rdt_protocol.h"


bool Protocol_Between(uint8_t low, uint8_t high, uint8_t test) {
    return ((low <= test && test < high) ||
            (high < low && low <= test) ||
            (test < high && high < low)
    );
}

