// Link stubs for test_hl2_ioboard_frames — composer-level driver.
//
// The harness links the real src/wire/FrameComposer.cpp and
// src/wire/Hl2IoBoard.cpp.  This file supplies what FrameComposer.cpp
// needs from the rest of the wire layer (prn storage, the dispatch
// globals RadioNet.cpp normally defines) and captures the EP2 datagram
// handed to metis_write_frame instead of sending it.

#include "wire/RadioNet.h"
#include "wire/RbpFilter.h"
#include "wire/MetisFrame.h"

#include <windows.h>

#include <cstdint>
#include <cstring>

namespace lyra::wire {

RadioNet::RadioNet()  = default;
RadioNet::~RadioNet() = default;

RadioNet*     prn           = nullptr;
int           XmitBit       = 0;
HPSDRModel    hpsdrModel    = HPSDRModel::HERMESLITE;
RadioProtocol radioProtocol = RadioProtocol::USB;
int           io_keep_running   = 0;
int           nddc              = 4;
unsigned char SampleRateIn2Bits = 0;
int           P1_en_diversity   = 0;
int           P1_adc_cntrl      = 0;
int           xvtr_enable       = 0;
int           ApolloFilt        = 0x04;   // create_rnet's safe default
int           ApolloFiltSelect  = 0;
int           ApolloTuner       = 0;
int           ApolloATU         = 0;

int metis_write_frame(int endpoint, const uint8_t* payload_1024);

}  // namespace lyra::wire

// Last datagram the composer sent (1024 bytes, both USB frames).
std::uint8_t g_hl2iob_test_dg[1024];
long g_hl2iob_test_dg_count = 0;

int lyra::wire::metis_write_frame(int, const uint8_t* payload_1024)
{
    std::memcpy(g_hl2iob_test_dg, payload_1024, sizeof g_hl2iob_test_dg);
    ++g_hl2iob_test_dg_count;
    return 1032;
}

// One-time wire-layer setup: prn + filter boards + the semaphore pair
// the composer releases after each datagram.
void hl2iob_test_wire_setup()
{
    using namespace lyra::wire;
    if (prn) return;
    prn = new RadioNet();
    prbpfilter = new RbpFilter();
    prbpfilter2 = new RbpFilter2();
    prn->hobbuffsRun[0] = CreateSemaphore(nullptr, 0, 1, nullptr);
    prn->hobbuffsRun[1] = CreateSemaphore(nullptr, 0, 1, nullptr);
}
