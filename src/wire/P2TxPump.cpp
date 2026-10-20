#include "P2TxPump.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <thread>

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

void applyProAudioPriority() {
#if defined(_WIN32)
    DWORD taskIndex = 0;
    HANDLE hTask = AvSetMmThreadCharacteristicsW(L"Pro Audio", &taskIndex);
    if (hTask)
        AvSetMmThreadPriority(hTask, AVRT_PRIORITY_HIGH);
    else
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
    SetThreadDescription(GetCurrentThread(), L"lyra-p2-txpump");
#endif
}

void waitUntil(void *timerHandle,
               std::chrono::steady_clock::time_point deadline) {
    using clock = std::chrono::steady_clock;
    const auto now = clock::now();
    if (now >= deadline)
        return;
#if defined(_WIN32)
    const auto ns =
        std::chrono::duration_cast<std::chrono::nanoseconds>(deadline - now)
            .count();
    HANDLE timer = static_cast<HANDLE>(timerHandle);
    if (!timer) {
        std::this_thread::sleep_until(deadline);
        return;
    }
    LARGE_INTEGER due;
    due.QuadPart = -std::max<long long>(1, ns / 100);
    if (SetWaitableTimer(timer, &due, 0, nullptr, nullptr, FALSE))
        WaitForSingleObject(timer, INFINITE);
#else
    (void)timerHandle;
    std::this_thread::sleep_until(deadline);
#endif
}

} // namespace

void P2TxPump::start() {
    stop();
    if (!inputSink_)
        return;
    stop_.store(false, std::memory_order_release);
    producedBlocks_.store(0, std::memory_order_relaxed);
    running_.store(true, std::memory_order_release);
    thread_ = std::thread(&P2TxPump::threadMain, this);
}

void P2TxPump::stop() {
    stop_.store(true, std::memory_order_release);
    if (thread_.joinable())
        thread_.join();
    running_.store(false, std::memory_order_release);
}

void P2TxPump::threadMain() {
    applyProAudioPriority();

#if defined(_WIN32)
    HANDLE timer = CreateWaitableTimerExW(
        nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,
        TIMER_ALL_ACCESS);
    if (!timer)
        timer = CreateWaitableTimerW(nullptr, TRUE, nullptr);
#else
    void *timer = nullptr;
#endif

    std::array<double, 2 * kBlockSamples> zeros{};
    using clock = std::chrono::steady_clock;
    const auto period = std::chrono::nanoseconds(
        1'000'000'000ll * kBlockSamples / kSampleRateHz);
    auto next = clock::now();

    while (!stop_.load(std::memory_order_relaxed)) {
        if (!inputSink_ || !inputSink_(zeros.data(), kBlockSamples)) {
            if (stop_.load(std::memory_order_relaxed))
                break;
            waitUntil(timer, clock::now() + period);
            continue;
        }
        producedBlocks_.fetch_add(1, std::memory_order_relaxed);
        next += period;
        const auto now = clock::now();
        if (now > next + 8 * period)
            next = now;
        else if (now < next)
            waitUntil(timer, next);
    }

#if defined(_WIN32)
    if (timer)
        CloseHandle(timer);
#endif
    running_.store(false, std::memory_order_release);
}

} // namespace lyra::wire
