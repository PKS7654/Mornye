#pragma once

#include <stdint.h>
#include <atomic>
#include <thread>
#include <vector>
#include <memory>
#include "uac_defs.h"
#include "uac_parser.h"
#include "ring_buffer.h"

class UacDriver {
public:
    UacDriver();
    ~UacDriver();

    bool init(int fd);
    void release();

    bool configureAndStart(uint32_t sampleRate, uint8_t bitDepth, uint8_t channels, size_t ringBufferBytes);
    void stop();

    size_t writePcm(const uint8_t* data, size_t length);
    bool setHardwareVolume(int percent); // 0 to 100

    bool isRunning() const { return m_isRunning.load(std::memory_order_relaxed); }
    uint32_t getCurrentSampleRate() const { return m_currentSampleRate; }
    uint8_t getCurrentBitDepth() const { return m_currentBitDepth; }

    const UacDeviceInfo& getDeviceInfo() const { return m_deviceInfo; }

private:
    bool setInterfaceAltSetting(uint8_t interfaceNumber, uint8_t altSetting);
    bool claimInterface(uint8_t interfaceNumber);
    bool releaseInterface(uint8_t interfaceNumber);

    bool setSampleRateUac1(uint8_t endpointAddr, uint32_t sampleRate);
    bool setSampleRateUac2(uint8_t clockSourceId, uint8_t interfaceNum, uint32_t sampleRate);

    void streamingThreadLoop();

    int m_fd;
    UacDeviceInfo m_deviceInfo;
    const UacAudioFormat* m_activeFormat;

    uint32_t m_currentSampleRate;
    uint8_t m_currentBitDepth;
    uint8_t m_currentChannels;
    size_t m_bytesPerFrame;

    std::unique_ptr<LockFreeAudioRingBuffer> m_ringBuffer;

    std::atomic<bool> m_isRunning;
    std::thread m_streamingThread;

    static constexpr size_t NUM_URBS = 4;
    static constexpr size_t PACKETS_PER_URB = 8;
};
