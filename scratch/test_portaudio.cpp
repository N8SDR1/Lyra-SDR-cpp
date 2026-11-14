// #158 DL-0 smoke — prove the vendored PortAudio 19.7.0 static lib
// builds, links, and its header resolves, and that the runtime can
// Pa_Initialize / enumerate / Pa_Terminate on this machine.  No Lyra
// code, no Qt — just the PortAudio surface the IVAC device layer (DL-1
// CallbackIVAC / StartAudioIVAC) will use.  Built on demand:
//   cmake --build build --target test_portaudio
//   test_portaudio --asio   also enables the ASIO host (Lyra opt-in gate,
//                           pa_win_hostapis.c) and runs a 2-in / 2-out
//                           duplex stream on the first ASIO driver for 1 s.
#include <portaudio.h>
#include <cstdio>
#include <cstring>

extern "C" {
void    PaLyra_SetAsioEnabled(int enabled);
int     PaLyra_AsioCompiledIn(void);
PaError PaLyra_AsioInitError(void);
}

static int g_frames = 0;
static int cb(const void *, void *out, unsigned long n,
              const PaStreamCallbackTimeInfo *, PaStreamCallbackFlags, void *) {
    std::memset(out, 0, n * 2 * sizeof(float));
    g_frames += static_cast<int>(n);
    return paContinue;
}

int main(int argc, char **argv) {
    const bool asio = argc > 1 && std::strcmp(argv[1], "--asio") == 0;
    std::printf("PortAudio version: %s\n", Pa_GetVersionText());
    std::printf("ASIO compiled in: %d, enabled for this run: %d\n",
                PaLyra_AsioCompiledIn(), asio ? 1 : 0);
    PaLyra_SetAsioEnabled(asio ? 1 : 0);

    PaError err = Pa_Initialize();
    if (err != paNoError) {
        std::printf("FAIL: Pa_Initialize -> %s\n", Pa_GetErrorText(err));
        return 1;
    }

    const int nDev = Pa_GetDeviceCount();
    const int nApi = Pa_GetHostApiCount();
    std::printf("Pa_Initialize OK: %d host APIs, %d devices\n", nApi, nDev);

    for (int i = 0; i < nApi; ++i) {
        const PaHostApiInfo *ha = Pa_GetHostApiInfo(i);
        if (ha)
            std::printf("  host API %d: %-16s (%d devices)\n",
                        i, ha->name, ha->deviceCount);
    }

    int rc = 0;
    if (asio) {
        std::printf("ASIO init error: %s\n",
                    Pa_GetErrorText(PaLyra_AsioInitError()));
        const PaHostApiIndex api = Pa_HostApiTypeIdToHostApiIndex(paASIO);
        const PaHostApiInfo *ha = api >= 0 ? Pa_GetHostApiInfo(api) : nullptr;
        if (!ha || ha->deviceCount < 1) {
            std::printf("FAIL: no ASIO driver found\n");
            rc = 1;
        } else {
            for (int j = 0; j < ha->deviceCount; ++j) {
                const PaDeviceIndex dev = Pa_HostApiDeviceIndexToDeviceIndex(api, j);
                const PaDeviceInfo *di = Pa_GetDeviceInfo(dev);
                std::printf("ASIO driver %d: '%s' %d in / %d out, default %.0f Hz\n",
                            j, di->name, di->maxInputChannels,
                            di->maxOutputChannels, di->defaultSampleRate);
                if (di->maxInputChannels < 2 || di->maxOutputChannels < 2) {
                    std::printf("  skipped (needs 2 in + 2 out)\n");
                    continue;
                }
                PaStreamParameters in{dev, 2, paFloat32,
                                      di->defaultLowInputLatency, nullptr};
                PaStreamParameters out{dev, 2, paFloat32,
                                       di->defaultLowOutputLatency, nullptr};
                PaStream *st = nullptr;
                err = Pa_OpenStream(&st, &in, &out, 48000, 256, 0, cb, nullptr);
                std::printf("  open 48 kHz duplex: %s\n", Pa_GetErrorText(err));
                if (err != paNoError) { rc = 1; continue; }
                err = Pa_StartStream(st);
                std::printf("  start: %s\n", Pa_GetErrorText(err));
                Pa_Sleep(1500);                 // let the driver settle
                const int f0 = g_frames;
                Pa_Sleep(1000);
                const int perSec = g_frames - f0;
                Pa_StopStream(st);
                Pa_CloseStream(st);
                std::printf("  callback frames in a steady 1 s: %d (expect ~48000)\n",
                            perSec);
                if (perSec < 44000 || perSec > 52000) rc = 1;
            }
        }
    }

    Pa_Terminate();
    std::printf(rc == 0 ? "DL-0 smoke OK\n" : "DL-0 smoke FAILED\n");
    return rc;
}
