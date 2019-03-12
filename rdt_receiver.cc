/*
 * FILE: rdt_receiver.cc
 * DESCRIPTION: Reliable data transfer receiver.
 * NOTE: This implementation assumes there is no packet loss, corruption, or 
 *       reordering.  You will need to enhance it to deal with all these 
 *       situations.  In this implementation, the packet format is laid out as 
 *       the following:
 *       
 *       |<-  1 byte  ->|<-             the rest            ->|
 *       | payload size |<-             payload             ->|
 *
 *       The first byte of each packet indicates the size of the payload
 *       (excluding this single-byte header)
 */


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <memory>
#include <vector>
#include "rdt_struct.h"
#include "rdt_receiver.h"
#include "rdt_protocol.h"

uint8_t frame_expected = 0;
uint8_t too_far = WINDOW_SIZE;
enum class RSLOT_STATE {
    ACKED, NONE
};
static std::vector<message *> sliding_window;

static std::vector<RSLOT_STATE> state_vec;

void Receiver_PrintContent();

void Receiver_Init() {
    fprintf(stdout, "At %.2fs: receiver initializing ...\n", GetSimulationTime());
    sliding_window.resize(MAX_SEQ);
    state_vec.assign(MAX_SEQ, RSLOT_STATE::NONE);

}


void Receiver_Final() {
    fprintf(stdout, "At %.2fs: receiver finalizing ...\n", GetSimulationTime());
    for (auto ptr:sliding_window) {
        if (ptr)
            delete ptr;
    }
}


void Receiver_FromLowerLayer(struct packet *pkt) {

    PktInMem p(pkt);


    // Corrupted packet
    if (p.isCorrupted())
        return;

    uint8_t seq = p.SEQ;

    // Prepare a acknowledged ack packet.
    PktInMem ACKP(0, seq);
    packet ACK_PKT;
    ACKP.CHECKSUM = ACKP.calChecksum();
    ACKP.toPacket(&ACK_PKT);

    // The packet falls into the slot and it hasn't been
    // received yet.
    if (Protocol_Between(frame_expected, too_far, seq) &&
        state_vec[seq] == RSLOT_STATE::NONE) {
        state_vec[seq] = RSLOT_STATE::ACKED;

        // Stash the message in sliding window
        message *msg = new message;
        msg->size = p.PAYLOAD_SIZE;
        msg->data = new char[p.PAYLOAD_SIZE];
        memcpy(msg->data, p.PAYLOAD, p.PAYLOAD_SIZE);
        sliding_window[seq] = msg;


        // Ack the non-corrupted packet
        Receiver_ToLowerLayer(&ACK_PKT);

        // CHeck whether we can move the boundary of the sliding window
        while (state_vec[frame_expected] == RSLOT_STATE::ACKED) {

            Receiver_ToUpperLayer(sliding_window[frame_expected]);
            delete sliding_window[frame_expected];
            sliding_window[frame_expected] = nullptr;
            state_vec[frame_expected] = RSLOT_STATE::NONE;

            uint8_t lo_next = (frame_expected + 1) % MAX_SEQ;
            uint8_t hi_next = (too_far + 1) % MAX_SEQ;
            frame_expected = lo_next;
            too_far = hi_next;


        }

        // If the sender send a packet whose seq < frame_expected,
        // the frame should fall into [frame_expected - WINDOW_SIZE, frame_expected)
    } else if (Protocol_Between((frame_expected + MAX_SEQ - WINDOW_SIZE) % MAX_SEQ, frame_expected, seq)) {


        // Resend the ack to sender
        Receiver_ToLowerLayer(&ACK_PKT);

        // The frame still be stashed because window hasn't slided yet.
    } else if (Protocol_Between(frame_expected, too_far, seq) &&
               state_vec[seq] == RSLOT_STATE::ACKED) {

        // Resend the ack to sender
        Receiver_ToLowerLayer(&ACK_PKT);

    }


}

