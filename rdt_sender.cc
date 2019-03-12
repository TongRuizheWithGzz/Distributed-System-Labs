

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>
#include <memory>
#include <list>
#include "rdt_struct.h"
#include "rdt_sender.h"
#include "rdt_protocol.h"

uint8_t next_pkt_to_send = 0;
uint8_t ack_expected = 0;
uint8_t n_buffered = 0;

enum class SSLOT_STATE {
    ACKED, NOT_ACKED, NONE
};

static std::vector<std::unique_ptr<PktInMem>> sliding_window;
static std::vector<SSLOT_STATE> state_vec;

static std::list<std::unique_ptr<PktInMem>> pkt_buff;


struct ScheduledEvent {
    uint8_t seq;
    double timeout;
    double startTime;

    ScheduledEvent(uint8_t seq, double timeout, double startTime)
            : seq(seq), timeout(timeout), startTime(startTime) {}
};

static std::list<std::unique_ptr<ScheduledEvent>> scheduled_timers;

static void Sender_stopTimer_seq(uint8_t);

static void Sender_Stash_Message(message *msg);

static void Sender_ExpireSW();

void Sender_PrintSWContent();


static void Sender_stopTimer_seq(uint8_t seq) {


    for (auto iter = scheduled_timers.cbegin(); iter != scheduled_timers.cend(); iter++) {
        // find the timer && the frame is the oldest one.
        // We have to reset the physical timer
        if ((*iter)->seq == seq && iter == scheduled_timers.cbegin()) {
            for (auto &ptr:scheduled_timers) {
                ptr->startTime = GetSimulationTime();
                ptr->timeout -= GetSimulationTime() - ptr->startTime;
            }
            // Set the timer to the new oldest frame
            iter = scheduled_timers.erase(iter);
            if (!scheduled_timers.empty())
                Sender_StartTimer((*iter)->timeout);
            else
                Sender_StopTimer();
            return;
            // Otherwise, just removing the timer for the seq is enough
        } else if ((*iter)->seq == seq) {
            scheduled_timers.erase(iter);
            return;
        }
    }
}


void Sender_Init() {
    fprintf(stdout, "At %.2fs: sender initializing ...\n", GetSimulationTime());

    // Expand the sliding window to WINDOW_SIZE
    // For simplicity, I decide to use a buffer of size MAX_SEQ
    // Of course we should a buffer of size MAX_SEQ + 1, but that will reduce the
    // readability of program and induce some complexity, since we have to maintain
    // a mapping from sequence number to sliding window index.
    sliding_window.resize(MAX_SEQ);
    state_vec.assign(MAX_SEQ, SSLOT_STATE::NONE);
}

void Sender_Final() {
    fprintf(stdout, "At %.2fs: sender finalizing ...\n", GetSimulationTime());
}


void Sender_FromUpperLayer(struct message *msg) {
    Sender_Stash_Message(msg);
    Sender_ExpireSW();
}

void Sender_ExpireSW() {


    while (n_buffered < WINDOW_SIZE && !pkt_buff.empty()) {

        // When sliding window is empty, how can there exist an timer?
        if (n_buffered == 0)
            Sender_StartTimer(TIMEOUT);


        // Pop a raw packet from pkt_buff and format the packet
        auto pkt = std::move(pkt_buff.front());
        pkt_buff.pop_front();
        pkt->SEQ = next_pkt_to_send;
        pkt->CHECKSUM = pkt->calChecksum();


        // Send the in-mem packet to physical layer
        packet p;
        pkt->toPacket(&p);
        Sender_ToLowerLayer(&p);

        // Buffer the packet in the sliding window slot
        // May resend the packet if it is timeout
        sliding_window[next_pkt_to_send] = std::move(pkt);
        state_vec[next_pkt_to_send] = SSLOT_STATE::NOT_ACKED;
        n_buffered = n_buffered + 1;

        // Add a new timer to scheduled_timers;
        auto event = std::unique_ptr<ScheduledEvent>(
                new ScheduledEvent(next_pkt_to_send, TIMEOUT, GetSimulationTime()));
        scheduled_timers.push_back(std::move(event));

        // Expand the sliding window at last
        next_pkt_to_send = (next_pkt_to_send + 1) % MAX_SEQ;

    }

//    Sender_PrintSWContent();

}


void Sender_FromLowerLayer(struct packet *pkt) {

    PktInMem pktInMem(pkt);

    // An corrupted ACK frame
    if (pktInMem.isCorrupted())
        return;


    uint8_t ack = pktInMem.ACK;

    // Ack is the low bound of sliding window
    if (ack == ack_expected) {
        state_vec[ack] = SSLOT_STATE::ACKED;
        Sender_stopTimer_seq(ack);

        // Try to slide as much as possible
        while (state_vec[ack_expected] == SSLOT_STATE::ACKED) {
            // Slide the window by move the lower bound.
            state_vec[ack_expected] = SSLOT_STATE::NONE;
            ack_expected = (ack_expected + 1) % MAX_SEQ;
            n_buffered = n_buffered - 1;
        }

    } else if (Protocol_Between(ack_expected, next_pkt_to_send, ack)
               && state_vec[ack] == SSLOT_STATE::NOT_ACKED) {
        state_vec[ack] = SSLOT_STATE::ACKED;
        Sender_stopTimer_seq(ack);
    }

    // There will be more slots in the sliding window!
    Sender_ExpireSW();
}

void Sender_Timeout() {

    packet p;

    // The time-out frame must be the one at the front of scheduled timers
    auto expired_timer = std::move(scheduled_timers.front());
    scheduled_timers.pop_front();

    // Resend the timeout packet
    uint8_t seq = expired_timer->seq;
    auto iter = sliding_window.begin() + seq;



    (*iter)->toPacket(&p);
    Sender_ToLowerLayer(&p);

    // Set a new timer for the frame and place
    // the timer to the back of the scheduled timers
    auto rescheduled_timer = std::move(expired_timer);
    rescheduled_timer->startTime = GetSimulationTime();
    rescheduled_timer->timeout = TIMEOUT;
    scheduled_timers.push_back(std::move(rescheduled_timer));

    // Adjust timeout and startTime of all events
    for (auto &e:scheduled_timers) {
        e->timeout -= (GetSimulationTime() - e->startTime);
        e->startTime = GetSimulationTime();
    }

    // Start a new timer
    Sender_StartTimer(scheduled_timers.front()->timeout);


}


void Sender_Stash_Message(message *msg) {
    int cursor = 0;
    while (msg->size - cursor > MAX_PAYLOAD_SIZE) {
        pkt_buff.emplace_back(new PktInMem(0, 0, MAX_PAYLOAD_SIZE, (uint8_t *) (msg->data + cursor)));
        cursor += MAX_PAYLOAD_SIZE;
    }
    if (msg->size > cursor)
        pkt_buff.emplace_back(new PktInMem(0, 0, msg->size - cursor, (uint8_t *) (msg->data + cursor)));
}


