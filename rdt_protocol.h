//
// Created by 同睿哲 on 2019/3/10.
//


#ifndef _RDT_PROTOCOL_H
#define _RDT_PROTOCOL_H

#include <cstdint>
#include <cstring>
#include <assert.h>
#include <algorithm>
#include "rdt_struct.h"

const uint8_t MAX_SEQ = 32;
const uint8_t WINDOW_SIZE = 10;
const double TIMEOUT = 0.3;
const uint8_t HEADER_SIZE = 5;
const uint8_t MAX_PAYLOAD_SIZE = (RDT_PKTSIZE - HEADER_SIZE);

struct PktInMem {
    uint16_t CHECKSUM;      // 16-bit checksum
    uint8_t SEQ;            // Sequence number
    uint8_t ACK;            // Acknowledge number
    uint8_t PAYLOAD_SIZE;   // Payload size
    uint8_t PAYLOAD[MAX_PAYLOAD_SIZE];

    PktInMem() = default;

    explicit PktInMem(uint8_t seq, uint8_t ack,
                      uint8_t Payload_size = 0, uint8_t *Payload = nullptr, uint16_t cs = 0)
            : CHECKSUM(cs), SEQ(seq), ACK(ack), PAYLOAD_SIZE(Payload_size) {
        memcpy(PAYLOAD, Payload, PAYLOAD_SIZE);

    }

    explicit PktInMem(packet *pkt) {
        assert(pkt);
        auto *ptr = (uint8_t *) pkt;
        memcpy(&CHECKSUM, ptr, 2);
        memcpy(&SEQ, ptr + 2, 1);
        memcpy(&ACK, ptr + 3, 1);
        memcpy(&PAYLOAD_SIZE, ptr + 4, 1);
        memcpy(PAYLOAD, ptr + 5, std::min(PAYLOAD_SIZE, MAX_PAYLOAD_SIZE));
    }

    bool isCorrupted() {
        if (SEQ != 0 && ACK != 0)
            return true;
        if (PAYLOAD_SIZE > MAX_PAYLOAD_SIZE)
            return true;
        if (calChecksum() != CHECKSUM)
            return true;
        return false;

    }

    uint16_t calChecksum() const {
        uint32_t cs = 0;
        cs = (SEQ << 8) + ACK;
        cs += PAYLOAD_SIZE << 8;

        uint32_t offset = 0;
        while ((offset + 1) < PAYLOAD_SIZE) {
            cs += (PAYLOAD[offset] << 8) + (PAYLOAD[offset + 1]);
            offset += 2;
        }
        if (PAYLOAD_SIZE % 2 != 0)
            cs += PAYLOAD[offset] << 8;

        cs = (cs >> 16) + (cs & 0xffff);
        if ((cs & 0xffff0000) != 0)
            cs = (cs >> 16) + (cs & 0xffff);
        return (uint16_t) ~cs;
    }

    void toPacket(packet *p) {
        auto *ptr = (uint8_t *) p;
        memcpy(ptr, &CHECKSUM, 2);
        memcpy(ptr + 2, &SEQ, 1);
        memcpy(ptr + 3, &ACK, 1);
        memcpy(ptr + 4, &PAYLOAD_SIZE, 1);
        memcpy(ptr + 5, PAYLOAD, PAYLOAD_SIZE);
    }
};

bool Protocol_Between(uint8_t low, uint8_t high, uint8_t test);


#endif
