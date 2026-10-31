#pragma once

#include <stdint.h>
#include <vector>
#include <string>
#include "uac_defs.h"

struct UacAudioFormat {
    uint8_t interfaceNumber;
    uint8_t altSetting;
    uint8_t endpointAddress;
    uint16_t maxPacketSize;
    uint8_t interval;
    uint8_t channels;
    uint8_t subslotSize;    // 2, 3, or 4 bytes
    uint8_t bitResolution;  // 16, 24, or 32 bits
    std::vector<uint32_t> supportedSampleRates;
    bool isUac2;
    uint8_t clockSourceId;  // for UAC2 clock control
    uint8_t featureUnitId;  // for hardware volume control
};

struct UacDeviceInfo {
    std::string deviceName;
    int uacVersion; // 1 or 2
    uint8_t acInterfaceNumber;
    uint8_t featureUnitId;
    uint8_t clockSourceId;
    std::vector<UacAudioFormat> formats;
};

class UacDescriptorParser {
public:
    static bool parseDescriptors(int fd, UacDeviceInfo& outInfo);
    
    static const UacAudioFormat* findBestFormat(
        const UacDeviceInfo& info,
        uint32_t sampleRate,
        uint8_t bitDepth,
        uint8_t channels
    );
};
