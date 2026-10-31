#include "include/uac_parser.h"
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/usbdevice_fs.h>
#include <android/log.h>
#include <cstring>
#include <algorithm>

#define TAG "MornyeUsbParser"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__)

bool UacDescriptorParser::parseDescriptors(int fd, UacDeviceInfo& outInfo) {
    if (fd < 0) {
        LOGE("Invalid file descriptor: %d", fd);
        return false;
    }

    uint8_t buffer[4096];
    memset(buffer, 0, sizeof(buffer));

    // Request USB Configuration Descriptor from device
    struct usbdevfs_ctrltransfer ctrl;
    memset(&ctrl, 0, sizeof(ctrl));
    ctrl.bRequestType = USB_DIR_IN | USB_TYPE_STANDARD | USB_RECIP_DEVICE;
    ctrl.bRequest = 0x06; // GET_DESCRIPTOR
    ctrl.wValue = (0x02 << 8); // CONFIGURATION descriptor index 0
    ctrl.wIndex = 0;
    ctrl.wLength = sizeof(buffer);
    ctrl.data = buffer;
    ctrl.timeout = 2000;

    int ret = ioctl(fd, USBDEVFS_CONTROL, &ctrl);
    if (ret < 9) {
        // Fallback: try reading directly from fd (standard Linux usbfs behavior)
        lseek(fd, 0, SEEK_SET);
        ret = read(fd, buffer, sizeof(buffer));
        if (ret < 9) {
            LOGE("Failed to read USB configuration descriptor (ret=%d)", ret);
            return false;
        }
    }

    uint16_t totalLength = buffer[2] | (buffer[3] << 8);
    if (totalLength > ret) totalLength = ret;

    LOGI("USB Configuration Descriptor Total Length: %u bytes", totalLength);

    size_t offset = 0;
    outInfo.uacVersion = 1;
    outInfo.acInterfaceNumber = 0;
    outInfo.featureUnitId = 0;
    outInfo.clockSourceId = 0;

    uint8_t currentInterface = 0;
    uint8_t currentAltSetting = 0;
    uint8_t currentSubclass = 0;
    bool inAudioStreaming = false;

    UacAudioFormat currentFormat;
    memset(&currentFormat, 0, sizeof(currentFormat));

    while (offset + 2 <= totalLength) {
        uint8_t len = buffer[offset];
        uint8_t type = buffer[offset + 1];

        if (len == 0 || offset + len > totalLength) {
            break;
        }

        const uint8_t* desc = buffer + offset;

        // Standard Interface Descriptor (0x04)
        if (type == 0x04 && len >= 9) {
            currentInterface = desc[2];
            currentAltSetting = desc[3];
            uint8_t intfClass = desc[5];
            currentSubclass = desc[6];
            uint8_t protocol = desc[7];

            if (intfClass == USB_CLASS_AUDIO) {
                if (currentSubclass == USB_SUBCLASS_AUDIOCONTROL) {
                    outInfo.acInterfaceNumber = currentInterface;
                    if (protocol == 0x20) { // IP_VERSION_02_00
                        outInfo.uacVersion = 2;
                    }
                    inAudioStreaming = false;
                } else if (currentSubclass == USB_SUBCLASS_AUDIOSTREAMING) {
                    inAudioStreaming = true;
                    if (currentAltSetting > 0) {
                        currentFormat.interfaceNumber = currentInterface;
                        currentFormat.altSetting = currentAltSetting;
                        currentFormat.isUac2 = (outInfo.uacVersion == 2);
                        currentFormat.clockSourceId = outInfo.clockSourceId;
                        currentFormat.featureUnitId = outInfo.featureUnitId;
                    }
                }
            } else {
                inAudioStreaming = false;
            }
        }
        // Class-Specific Interface Descriptor (0x24)
        else if (type == CS_INTERFACE && len >= 3) {
            uint8_t subtype = desc[2];

            // In AudioControl interface:
            if (!inAudioStreaming && currentSubclass == USB_SUBCLASS_AUDIOCONTROL) {
                if (subtype == UAC_FEATURE_UNIT && len >= 6) {
                    // Feature unit for hardware volume
                    outInfo.featureUnitId = desc[3];
                    LOGI("Found Audio Feature Unit ID: 0x%02x", outInfo.featureUnitId);
                } else if (subtype == UAC2_CLOCK_SOURCE && len >= 8) {
                    // UAC 2.0 Clock Source
                    outInfo.clockSourceId = desc[3];
                    outInfo.uacVersion = 2;
                    LOGI("Found UAC 2.0 Clock Source ID: 0x%02x", outInfo.clockSourceId);
                }
            }
            // In AudioStreaming interface:
            else if (inAudioStreaming && currentAltSetting > 0) {
                if (subtype == UAC_FORMAT_TYPE) {
                    uint8_t formatType = desc[3];
                    if (formatType == UAC_FORMAT_TYPE_I) {
                        if (outInfo.uacVersion == 1 && len >= 8) {
                            currentFormat.channels = desc[4];
                            currentFormat.subslotSize = desc[5];
                            currentFormat.bitResolution = desc[6];
                            uint8_t samFreqType = desc[7];

                            currentFormat.supportedSampleRates.clear();
                            if (samFreqType == 0) {
                                // Continuous range (min to max)
                                uint32_t minRate = desc[8] | (desc[9] << 8) | (desc[10] << 16);
                                uint32_t maxRate = desc[11] | (desc[12] << 8) | (desc[13] << 16);
                                for (uint32_t r : {44100u, 48000u, 88200u, 96000u, 176400u, 192000u}) {
                                    if (r >= minRate && r <= maxRate) {
                                        currentFormat.supportedSampleRates.push_back(r);
                                    }
                                }
                            } else {
                                // Discrete sample rates
                                for (uint8_t i = 0; i < samFreqType && (8 + i * 3 + 2 < len); i++) {
                                    uint32_t rate = desc[8 + i * 3] |
                                                   (desc[8 + i * 3 + 1] << 8) |
                                                   (desc[8 + i * 3 + 2] << 16);
                                    currentFormat.supportedSampleRates.push_back(rate);
                                }
                            }
                            LOGI("UAC1 AltSetting %d: %d ch, %d-bit, %lu sample rates",
                                 currentAltSetting, currentFormat.channels, currentFormat.bitResolution,
                                 (unsigned long)currentFormat.supportedSampleRates.size());
                        } else if (outInfo.uacVersion == 2 && len >= 6) {
                            currentFormat.subslotSize = desc[4];
                            currentFormat.bitResolution = desc[5];
                            currentFormat.channels = 2; // Default stereo for UAC2 format I
                            // UAC 2.0 DACs typically support 44.1k, 48k, 96k, 192k
                            currentFormat.supportedSampleRates = {44100, 48000, 88200, 96000, 176400, 192000};
                            LOGI("UAC2 AltSetting %d: %d-bit (subslot %d bytes)",
                                 currentAltSetting, currentFormat.bitResolution, currentFormat.subslotSize);
                        }
                    }
                }
            }
        }
        // Standard Endpoint Descriptor (0x05)
        else if (type == 0x05 && len >= 7 && inAudioStreaming && currentAltSetting > 0) {
            uint8_t epAddr = desc[2];
            uint8_t epAttr = desc[3];
            uint16_t maxPacket = desc[4] | (desc[5] << 8);
            uint8_t interval = desc[6];

            // OUT Isochronous Endpoint (Direction = OUT, Transfer = Isochronous)
            if ((epAddr & 0x80) == 0 && (epAttr & 0x03) == 0x01) {
                currentFormat.endpointAddress = epAddr;
                currentFormat.maxPacketSize = maxPacket & 0x07FF; // mask off high-bandwidth bits
                currentFormat.interval = interval;

                LOGI("Found Audio OUT Isochronous EP: 0x%02x, MaxPacket: %u, Interval: %u",
                     epAddr, currentFormat.maxPacketSize, interval);

                // Add configured format to device info list
                if (currentFormat.channels > 0 && currentFormat.bitResolution > 0) {
                    outInfo.formats.push_back(currentFormat);
                }
            }
        }

        offset += len;
    }

    LOGI("Discovered %zu valid audio streaming formats on USB device.", outInfo.formats.size());
    return !outInfo.formats.empty();
}

const UacAudioFormat* UacDescriptorParser::findBestFormat(
    const UacDeviceInfo& info,
    uint32_t sampleRate,
    uint8_t bitDepth,
    uint8_t channels
) {
    const UacAudioFormat* bestMatch = nullptr;

    for (const auto& fmt : info.formats) {
        if (fmt.channels != channels && fmt.channels != 2) continue;

        // Check if sample rate is supported
        bool rateSupported = false;
        for (uint32_t r : fmt.supportedSampleRates) {
            if (r == sampleRate) {
                rateSupported = true;
                break;
            }
        }

        if (!rateSupported) continue;

        // Check bit depth
        if (fmt.bitResolution == bitDepth) {
            return &fmt; // Exact match!
        }

        // If exact bit depth not found, prefer 24-bit or 32-bit container
        if (fmt.bitResolution >= bitDepth && (!bestMatch || fmt.bitResolution < bestMatch->bitResolution)) {
            bestMatch = &fmt;
        }
    }

    // Fallback: return first format that supports the sample rate
    if (!bestMatch) {
        for (const auto& fmt : info.formats) {
            for (uint32_t r : fmt.supportedSampleRates) {
                if (r == sampleRate) {
                    return &fmt;
                }
            }
        }
    }

    return bestMatch ? bestMatch : (info.formats.empty() ? nullptr : &info.formats[0]);
}
