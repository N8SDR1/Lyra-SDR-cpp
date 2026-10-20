#include "P2TxWriter.h"

#include <QDebug>

#include <chrono>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002
#endif
#include <windows.h>
#include <avrt.h>
#pragma comment(lib, "avrt.lib")
#endif

namespace lyra::wire {
namespace {

void applyProAudioPriority(const wchar_t *name) {
#if defined(_WIN32)
    DWORD taskIndex = 0;
    HANDLE hTask = AvSetMmThreadCharacteristicsW(L"Pro Audio", &taskIndex);
    if (hTask)
        AvSetMmThreadPriority(hTask, AVRT_PRIORITY_HIGH);
    else
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
    using SetThreadDescriptionFn = HRESULT(WINAPI *)(HANDLE, PCWSTR);
    if (const auto fn = reinterpret_cast<SetThreadDescriptionFn>(GetProcAddress(
            GetModuleHandleW(L"kernel32.dll"), "SetThreadDescription"))) {
        fn(GetCurrentThread(), name);
    }
#else
    (void)name;
#endif
}

void sleepMs(int ms) {
#if defined(_WIN32)
    HANDLE timer = CreateWaitableTimerExW(
        nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,
        TIMER_ALL_ACCESS);
    if (!timer)
        timer = CreateWaitableTimerW(nullptr, TRUE, nullptr);
    if (!timer) {
        Sleep(static_cast<DWORD>(ms));
        return;
    }
    LARGE_INTEGER due;
    due.QuadPart = -static_cast<LONGLONG>(ms) * 10'000;
    if (SetWaitableTimer(timer, &due, 0, nullptr, nullptr, FALSE))
        WaitForSingleObject(timer, INFINITE);
    CloseHandle(timer);
#else
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
#endif
}

} // namespace

void P2TxWriter::startFromInput() {
    stop();
    if (!inputFifo_ || !packetSink_)
        return;
    stop_.store(false, std::memory_order_release);
    sequence_.store(0, std::memory_order_relaxed);
    packetsSent_.store(0, std::memory_order_relaxed);
    running_.store(true, std::memory_order_release);
    thread_ = std::thread(&P2TxWriter::threadMain, this);
}

void P2TxWriter::stop() {
    stop_.store(true, std::memory_order_release);
    if (inputFifo_)
        inputFifo_->wakeWaiters();
    if (thread_.joinable())
        thread_.join();
    running_.store(false, std::memory_order_release);
}

void P2TxWriter::threadMain() {
    applyProAudioPriority(L"lyra-p2-txiq");

    using clock = std::chrono::steady_clock;
    auto last = clock::now();
    double fifoSamples = 0.0;
    bool loggedSendFail = false;

    while (!stop_.load(std::memory_order_relaxed)) {
        if (!inputFifo_->waitForPacket(stop_, 5))
            continue;
        if (stop_.load(std::memory_order_relaxed))
            break;

        std::array<P2TxIqSample, P2TxPackets::kIqSamplesPerPacket> samples;
        if (!inputFifo_->popPacket(samples))
            continue;

        const auto now = clock::now();
        const double dt =
            std::chrono::duration<double>(now - last).count();
        last = now;
        fifoSamples -= dt * static_cast<double>(kSampleRateHz);
        if (fifoSamples < 0.0)
            fifoSamples = 0.0;
        // DeskHPSDR wired threshold (~1250 samples). If we are ahead of
        // the DUC, pause 1 ms instead of bursting stale IQ.
        if (fifoSamples > 1250.0)
            sleepMs(1);

        const QByteArray packet = P2TxPackets::encodeIq(
            sequence_.load(std::memory_order_relaxed), samples);
        if (!packetSink_(packet)) {
            if (!loggedSendFail) {
                loggedSendFail = true;
                qWarning("[p2tx] DUC-IQ sendto failed — writer stays up");
            }
        }
        fifoSamples += static_cast<double>(P2TxPackets::kIqSamplesPerPacket);
        sequence_.fetch_add(1, std::memory_order_relaxed);
        packetsSent_.fetch_add(1, std::memory_order_relaxed);
    }

    running_.store(false, std::memory_order_release);
}

} // namespace lyra::wire
