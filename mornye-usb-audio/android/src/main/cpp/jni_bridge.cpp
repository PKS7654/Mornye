#include <jni.h>
#include <string>
#include <memory>
#include "include/uac_driver.h"
#include <android/log.h>

#define TAG "MornyeUsbJni"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__)

static std::unique_ptr<UacDriver> g_driver;

extern "C" {

JNIEXPORT jboolean JNICALL
Java_com_mornye_usbaudio_UsbAudioEngine_nativeInit(JNIEnv* env, jobject thiz, jint fd) {
    if (!g_driver) {
        g_driver = std::make_unique<UacDriver>();
    }
    return g_driver->init(fd) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jstring JNICALL
Java_com_mornye_usbaudio_UsbAudioEngine_nativeGetCapabilities(JNIEnv* env, jobject thiz) {
    if (!g_driver) {
        return env->NewStringUTF("[]");
    }

    const auto& devInfo = g_driver->getDeviceInfo();
    std::string json = "{\"uac_version\":" + std::to_string(devInfo.uacVersion) + ",\"formats\":[";

    for (size_t i = 0; i < devInfo.formats.size(); i++) {
        const auto& fmt = devInfo.formats[i];
        if (i > 0) json += ",";
        json += "{\"bit_depth\":" + std::to_string(fmt.bitResolution);
        json += ",\"channels\":" + std::to_string(fmt.channels);
        json += ",\"rates\":[";
        for (size_t r = 0; r < fmt.supportedSampleRates.size(); r++) {
            if (r > 0) json += ",";
            json += std::to_string(fmt.supportedSampleRates[r]);
        }
        json += "]}";
    }
    json += "]}";

    return env->NewStringUTF(json.c_str());
}

JNIEXPORT jboolean JNICALL
Java_com_mornye_usbaudio_UsbAudioEngine_nativeStart(
    JNIEnv* env,
    jobject thiz,
    jint sampleRate,
    jint bitDepth,
    jint channels,
    jint bufferSizeBytes
) {
    if (!g_driver) {
        return JNI_FALSE;
    }
    return g_driver->configureAndStart(sampleRate, bitDepth, channels, bufferSizeBytes) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jint JNICALL
Java_com_mornye_usbaudio_UsbAudioEngine_nativeWritePcm(
    JNIEnv* env,
    jobject thiz,
    jbyteArray pcmData,
    jint offset,
    jint length
) {
    if (!g_driver || !pcmData || length <= 0) {
        return 0;
    }

    jbyte* bytes = env->GetByteArrayElements(pcmData, nullptr);
    if (!bytes) return 0;

    size_t written = g_driver->writePcm(reinterpret_cast<const uint8_t*>(bytes + offset), length);

    env->ReleaseByteArrayElements(pcmData, bytes, JNI_ABORT);
    return static_cast<jint>(written);
}

JNIEXPORT jint JNICALL
Java_com_mornye_usbaudio_UsbAudioEngine_nativeWriteDirect(
    JNIEnv* env,
    jobject thiz,
    jobject byteBuffer,
    jint length
) {
    if (!g_driver || !byteBuffer || length <= 0) {
        return 0;
    }

    void* bufferAddress = env->GetDirectBufferAddress(byteBuffer);
    if (!bufferAddress) return 0;

    size_t written = g_driver->writePcm(reinterpret_cast<const uint8_t*>(bufferAddress), length);
    return static_cast<jint>(written);
}

JNIEXPORT jboolean JNICALL
Java_com_mornye_usbaudio_UsbAudioEngine_nativeSetVolume(JNIEnv* env, jobject thiz, jint percent) {
    if (!g_driver) return JNI_FALSE;
    return g_driver->setHardwareVolume(percent) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL
Java_com_mornye_usbaudio_UsbAudioEngine_nativeStop(JNIEnv* env, jobject thiz) {
    if (g_driver) {
        g_driver->stop();
    }
}

JNIEXPORT void JNICALL
Java_com_mornye_usbaudio_UsbAudioEngine_nativeRelease(JNIEnv* env, jobject thiz) {
    if (g_driver) {
        g_driver->release();
        g_driver.reset();
    }
}

} // extern "C"
