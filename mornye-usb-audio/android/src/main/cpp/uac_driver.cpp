#include "include/uac_driver.h"
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/resource.h>
#include <pthread.h>
#include <android/log.h>
#include <cstring>
#include <cmath>

#define TAG "MornyeUsbDriver"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__)

UacDriver::UacDriver()
    : m_fd(-1),
      m_activeFormat(nullptr),
      m_currentSampleRate(0),
      m_currentBitDepth(0),
      m_currentChannels(0),
      m_bytesPerFrame(0),
      m_isRunning(false) {}

UacDriver::~UacDriver() {
    release();
}

bool UacDriver::init(int fd) {
    if (fd < 0) {
        LOGE("Invalid USB file descriptor: %d", fd);
        return false;
    }

    m_fd = fd;
    m_deviceInfo.formats.clear();

    if (!UacDescriptorParser::parseDescriptors(m_fd, m_deviceInfo)) {
        LOGE("Failed to parse UAC descriptors!");
        return false;
    }

    LOGI("USB Audio Engine initialized successfully (UAC v%d, %zu streaming formats)",
         m_deviceInfo.uacVersion, m_deviceInfo.formats.size());
    return true;
}

void UacDriver::release() {
    stop();

    if (m_fd >= 0) {
        if (m_activeFormat) {
            releaseInterface(m_activeFormat->interfaceNumber);
        }
        if (m_deviceInfo.acInterfaceNumber > 0) {
            releaseInterface(m_deviceInfo.acInterfaceNumber);
        }
        m_fd = -1;
    }
    m_activeFormat = nullptr;
}

bool UacDriver::claimInterface(uint8_t interfaceNumber) {
    unsigned int intf = interfaceNumber;
    int ret = ioctl(m_fd, USBDEVFS_CLAIMINTERFACE, &intf);
    if (ret < 0) {
        LOGW("Failed to claim USB interface %u (ret=%d)", interfaceNumber, ret);
        return false;
    }
    LOGI("Claimed USB interface %u", interfaceNumber);
    return true;
}

bool UacDriver::releaseInterface(uint8_t interfaceNumber) {
    unsigned int intf = interfaceNumber;
    int ret = ioctl(m_fd, USBDEVFS_RELEASEINTERFACE, &intf);
    return ret >= 0;
}

bool UacDriver::setInterfaceAltSetting(uint8_t interfaceNumber, uint8_t altSetting) {
    struct usbdevfs_setinterface setintf;
    setintf.interface = interfaceNumber;
    setintf.altsetting = altSetting;

    int ret = ioctl(m_fd, USBDEVFS_SETINTERFACE, &setintf);
    if (ret < 0) {
        LOGE("Failed to set interface %u to AltSetting %u (ret=%d)",
             interfaceNumber, altSetting, ret);
        return false;
    }
    LOGI("Set interface %u to AltSetting %u", interfaceNumber, altSetting);
    return true;
}

bool UacDriver::setSampleRateUac1(uint8_t endpointAddr, uint32_t sampleRate) {
    uint8_t rateBytes[3];
    rateBytes[0] = sampleRate & 0xFF;
    rateBytes[1] = (sampleRate >> 8) & 0xFF;
    rateBytes[2] = (sampleRate >> 16) & 0xFF;

    struct usbdevfs_ctrltransfer ctrl;
    memset(&ctrl, 0, sizeof(ctrl));
    ctrl.bRequestType = USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_ENDPOINT;
    ctrl.bRequest = UAC_SET_CUR;
    ctrl.wValue = (UAC_EP_CS_SAMPLING_FREQ_CONTROL << 8);
    ctrl.wIndex = endpointAddr;
    ctrl.wLength = sizeof(rateBytes);
    ctrl.data = rateBytes;
    ctrl.timeout = 1000;

    int ret = ioctl(m_fd, USBDEVFS_CONTROL, &ctrl);
    if (ret < 0) {
        LOGW("UAC1 set sample rate (%u Hz) failed on EP 0x%02x (ret=%d)", sampleRate, endpointAddr, ret);
        return false;
    }
    LOGI("UAC1 Hardware sample rate locked to %u Hz on EP 0x%02x", sampleRate, endpointAddr);
    return true;
}

bool UacDriver::setSampleRateUac2(uint8_t clockSourceId, uint8_t interfaceNum, uint32_t sampleRate) {
    uint32_t rateLe = sampleRate;

    struct usbdevfs_ctrltransfer ctrl;
    memset(&ctrl, 0, sizeof(ctrl));
    ctrl.bRequestType = USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_INTERFACE;
    ctrl.bRequest = UAC2_CS_CUR;
    ctrl.wValue = (UAC2_CS_CONTROL_SAM_FREQ << 8);
    ctrl.wIndex = (clockSourceId << 8) | interfaceNum;
    ctrl.wLength = sizeof(rateLe);
    ctrl.data = &rateLe;
    ctrl.timeout = 1000;

    int ret = ioctl(m_fd, USBDEVFS_CONTROL, &ctrl);
    if (ret < 0) {
        LOGW("UAC2 set sample rate (%u Hz) failed on Clock 0x%02x (ret=%d)", sampleRate, clockSourceId, ret);
        return false;
    }
    LOGI("UAC2 Hardware Clock Source (0x%02x) locked to %u Hz (Bit-Perfect!)", clockSourceId, sampleRate);
    return true;
}

bool UacDriver::setHardwareVolume(int percent) {
    if (m_fd < 0 || !m_activeFormat || m_activeFormat->featureUnitId == 0) {
        return false;
    }

    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;

    // Convert 0..100 linear percentage to UAC 8.8 fixed-point decibels (-96 dB to 0 dB)
    // 0x0000 = 0 dB, 0x8000 = -128 dB. In UAC, 1 dB = 0x0100 (256 units).
    int16_t volumeDb;
    if (percent == 0) {
        volumeDb = -32768; // Mute (-128 dB)
    } else {
        float gainDb = 20.0f * log10f(percent / 100.0f); // -96 dB to 0 dB
        volumeDb = static_cast<int16_t>(gainDb * 256.0f);
    }

    struct usbdevfs_ctrltransfer ctrl;
    memset(&ctrl, 0, sizeof(ctrl));
    ctrl.bRequestType = USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_INTERFACE;
    ctrl.bRequest = UAC_SET_CUR;
    // wValue high byte: VOLUME_CONTROL (0x02), low byte: Channel 0 (Master) or 1
    ctrl.wValue = (UAC_FU_VOLUME << 8) | 0x00;
    ctrl.wIndex = (m_activeFormat->featureUnitId << 8) | m_deviceInfo.acInterfaceNumber;
    ctrl.wLength = sizeof(volumeDb);
    ctrl.data = &volumeDb;
    ctrl.timeout = 1000;

    int ret = ioctl(m_fd, USBDEVFS_CONTROL, &ctrl);
    return ret >= 0;
}

bool UacDriver::configureAndStart(
    uint32_t sampleRate,
    uint8_t bitDepth,
    uint8_t channels,
    size_t ringBufferBytes
) {
    if (m_fd < 0) {
        LOGE("Cannot start stream: USB device not open!");
        return false;
    }

    stop();

    // 1. Find matching audio streaming format
    m_activeFormat = UacDescriptorParser::findBestFormat(m_deviceInfo, sampleRate, bitDepth, channels);
    if (!m_activeFormat) {
        LOGE("No compatible USB audio format found for %u Hz, %u-bit, %u ch",
             sampleRate, bitDepth, channels);
        return false;
    }

    m_currentSampleRate = sampleRate;
    m_currentBitDepth = m_activeFormat->bitResolution;
    m_currentChannels = channels;
    m_bytesPerFrame = m_currentChannels * m_activeFormat->subslotSize;

    LOGI("Selected USB Audio Format: Intf %u, Alt %u, EP 0x%02x, %u-bit, %u bytes/frame",
         m_activeFormat->interfaceNumber, m_activeFormat->altSetting,
         m_activeFormat->endpointAddress, m_currentBitDepth, (unsigned int)m_bytesPerFrame);

    // 2. Claim AudioStreaming Interface
    claimInterface(m_activeFormat->interfaceNumber);

    // 3. Switch to operational AltSetting
    if (!setInterfaceAltSetting(m_activeFormat->interfaceNumber, m_activeFormat->altSetting)) {
        LOGE("Failed to set operational AltSetting!");
        return false;
    }

    // 4. Configure Hardware Sample Rate Clock
    if (m_activeFormat->isUac2 && m_activeFormat->clockSourceId > 0) {
        setSampleRateUac2(m_activeFormat->clockSourceId, m_activeFormat->interfaceNumber, sampleRate);
    } else {
        setSampleRateUac1(m_activeFormat->endpointAddress, sampleRate);
    }

    // 5. Allocate Lock-Free Ring Buffer
    if (ringBufferBytes < 65536) ringBufferBytes = 131072; // default 128KB (~680ms buffer at 48k/24b)
    m_ringBuffer = std::make_unique<LockFreeAudioRingBuffer>(ringBufferBytes);

    // 6. Launch Real-Time Isochronous Streaming Thread
    m_isRunning.store(true, std::memory_order_release);
    m_streamingThread = std::thread(&UacDriver::streamingThreadLoop, this);

    LOGI("Bit-Perfect USB Isochronous Streaming Active!");
    return true;
}

size_t UacDriver::writePcm(const uint8_t* data, size_t length) {
    if (!m_ringBuffer || !m_isRunning.load(std::memory_order_relaxed)) {
        return 0;
    }
    return m_ringBuffer->write(data, length);
}

void UacDriver::stop() {
    if (m_isRunning.exchange(false, std::memory_order_acq_rel)) {
        if (m_streamingThread.joinable()) {
            m_streamingThread.join();
        }
    }

    if (m_activeFormat && m_fd >= 0) {
        // Switch back to AltSetting 0 (Zero-Bandwidth idle state)
        setInterfaceAltSetting(m_activeFormat->interfaceNumber, 0);
    }

    m_ringBuffer.reset();
}

void UacDriver::streamingThreadLoop() {
    // Elevate thread priority to real-time audio
    setpriority(PRIO_PROCESS, 0, -19);

    pthread_t thisThread = pthread_self();
    struct sched_param param;
    param.sched_priority = sched_get_priority_max(SCHED_FIFO);
    pthread_setschedparam(thisThread, SCHED_FIFO, &param);

    // Calculate packet size per microframe/frame
    // At 48000 Hz, 1ms frame = 48 samples * m_bytesPerFrame
    // For 24-bit stereo (6 bytes/frame): 48 * 6 = 288 bytes per packet
    size_t samplesPerPacket = m_currentSampleRate / 1000;
    size_t packetSize = samplesPerPacket * m_bytesPerFrame;
    if (packetSize > m_activeFormat->maxPacketSize) {
        packetSize = m_activeFormat->maxPacketSize;
    }

    size_t urbBufferSize = packetSize * PACKETS_PER_URB;
    size_t totalUrbAlloc = sizeof(struct usbdevfs_urb) + sizeof(struct usbdevfs_iso_packet_desc) * PACKETS_PER_URB;

    // Allocate URB structures & memory buffers
    std::vector<uint8_t> urbMemory(NUM_URBS * totalUrbAlloc);
    std::vector<std::vector<uint8_t>> urbBuffers(NUM_URBS, std::vector<uint8_t>(urbBufferSize));
    struct usbdevfs_urb* urbs[NUM_URBS];

    for (size_t i = 0; i < NUM_URBS; i++) {
        urbs[i] = reinterpret_cast<struct usbdevfs_urb*>(urbMemory.data() + i * totalUrbAlloc);
        memset(urbs[i], 0, totalUrbAlloc);

        urbs[i]->type = USBDEVFS_URB_TYPE_ISO;
        urbs[i]->endpoint = m_activeFormat->endpointAddress;
        urbs[i]->flags = USBDEVFS_URB_ISO_ASAP;
        urbs[i]->buffer = urbBuffers[i].data();
        urbs[i]->buffer_length = static_cast<int>(urbBufferSize);
        urbs[i]->number_of_packets = PACKETS_PER_URB;
        urbs[i]->usercontext = reinterpret_cast<void*>(i);

        for (size_t p = 0; p < PACKETS_PER_URB; p++) {
            urbs[i]->iso_frame_desc[p].length = static_cast<unsigned int>(packetSize);
        }

        // Pre-fill with silence and initial PCM data
        m_ringBuffer->readWithSilencePadding(urbBuffers[i].data(), urbBufferSize);

        // Submit initial URBs
        int ret = ioctl(m_fd, USBDEVFS_SUBMITURB, urbs[i]);
        if (ret < 0) {
            LOGE("Failed to submit initial URB %zu (ret=%d)", i, ret);
        }
    }

    LOGI("USB Isochronous Streaming Pipeline Active (%zu URBs queued)", NUM_URBS);

    // Continuous streaming loop
    while (m_isRunning.load(std::memory_order_relaxed)) {
        struct usbdevfs_urb* reapedUrb = nullptr;

        // Reap completed URB
        int ret = ioctl(m_fd, USBDEVFS_REAPURB, &reapedUrb);
        if (ret < 0 || !reapedUrb) {
            if (errno == EAGAIN || errno == EINTR) {
                continue;
            }
            LOGE("USBDEVFS_REAPURB error (errno=%d)", errno);
            break;
        }

        size_t urbIdx = reinterpret_cast<size_t>(reapedUrb->usercontext);
        if (urbIdx >= NUM_URBS) continue;

        // Fill reaped URB with fresh PCM audio from the lock-free ring buffer
        m_ringBuffer->readWithSilencePadding(urbBuffers[urbIdx].data(), urbBufferSize);

        // Re-submit URB back to the kernel USB controller
        ret = ioctl(m_fd, USBDEVFS_SUBMITURB, reapedUrb);
        if (ret < 0) {
            LOGW("Failed to resubmit URB %zu (ret=%d)", urbIdx, ret);
        }
    }

    // Cancel in-flight URBs
    for (size_t i = 0; i < NUM_URBS; i++) {
        ioctl(m_fd, USBDEVFS_DISCARDURB, urbs[i]);
    }

    // Drain reaped URBs after discard
    for (size_t i = 0; i < NUM_URBS; i++) {
        struct usbdevfs_urb* drained = nullptr;
        ioctl(m_fd, USBDEVFS_REAPURBNDELAY, &drained);
    }

    LOGI("USB Isochronous Streaming Thread Stopped cleanly.");
}
