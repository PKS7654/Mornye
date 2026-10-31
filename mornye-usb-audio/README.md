# Mornye Bit-Perfect USB Audio Module (`mornye-usb-audio`)

A high-performance, user-space **USB Audio Class 1.0 & 2.0 (UAC1/UAC2)** native driver and Flutter plugin designed for **Mornye** (`mornyeapp/Mornye`).

This module enables Mornye to stream uncompressed **24-bit / 48 kHz (and higher) studio master PCM** directly to external USB DACs (such as Apple USB-C EarPods, Cirrus Logic CS43131, ESS Sabre, Realtek ALC5686) **bypassing Android's AudioFlinger, 48kHz float resampler, volume mixer, and Dirac DSP completely**.

---

## 1. Why Standard Android Audio Cannot Be Bit-Perfect

On stock Android (and particularly on MediaTek Helio G85 devices like the Realme Narzo 30A):
1. **AudioFlinger Mixer:** All outputs go through `MixerThread` (`PCM_FLOAT` 32-bit floating point, resampled).
2. **Vendor Sound Effects:** All streams are routed through `/odm/lib/soundfx/libdirac.so` (Dirac audio processing).
3. **No Direct Audio Route:** The vendor HAL audio policy (`usb_audio_policy_configuration.xml`) only registers `AUDIO_OUTPUT_FLAG_PRIMARY`, completely omitting `AUDIO_OUTPUT_FLAG_DIRECT`.
4. **No AAudio MMAP:** The MediaTek BSP lacks kernel NOIRQ MMAP support (`dumpsys media.aaudio` reports service not found).

### The Solution: User-Space USB Audio (HiBy Music Architecture)
Like **HiBy Music** and **USB Audio Player PRO (UAPP)**, this module takes full ownership of the USB DAC via Android's `UsbManager`, retrieves the underlying Linux file descriptor (`fd`), and executes native Linux ALSA `sound/usb/` Isochronous transfers in C++:

```
[Mornye Flutter / Rust Engine] (Decodes FLAC / ALAC / WAV)
          │
          ▼ (Decoded 24-bit / 48 kHz PCM frames)
[Lock-Free Atomic Ring Buffer (SPSC)] (Zero-allocation, wait-free)
          │
          ▼ (Isochronous URBs via Linux ioctl USBDEVFS_SUBMITURB)
[Direct USB Host Controller (/dev/bus/usb/)]
          │
          ▼ (Hardware Clock Locked via SET_CUR)
[Apple USB-C EarPods / External Hi-Fi DAC] (100% BIT-PERFECT!)
```

---

## 2. Architecture & References

This implementation strictly references:
- **Linux ALSA USB-Audio Kernel Driver (`sound/usb/`):**
  - `sound/usb/card.h` & `sound/usb/format.h`: USB Audio Class 1.0 & 2.0 descriptor parsing.
  - `sound/usb/clock.c`: UAC2 Clock Source entity sample rate locking via `SET_CUR` (`0x0100`).
  - `sound/usb/endpoint.c`: Isochronous asynchronous URB packet scheduling.
  - `sound/usb/mixer.c`: Hardware volume control via Feature Unit (`0x06`) decibel scaling.
- **Android `libusb` & `usbdevice_fs`:**
  - Zero-root permission acquisition using Android `UsbManager`.
  - Direct file descriptor mapping (`connection.getFileDescriptor()`) with `ioctl(USBDEVFS_*)`.

---

## 3. Directory Layout

```
mornye-usb-audio/
├── android/
│   ├── build.gradle
│   └── src/main/
│       ├── AndroidManifest.xml
│       ├── cpp/
│       │   ├── CMakeLists.txt
│       │   ├── include/
│       │   │   ├── ring_buffer.h      # Lock-free atomic circular buffer
│       │   │   ├── uac_defs.h         # Linux sound/usb/ UAC1/UAC2 definitions
│       │   │   ├── uac_driver.h       # Isochronous URB streaming engine
│       │   │   └── uac_parser.h       # USB descriptor parser
│       │   ├── uac_parser.cpp
│       │   ├── uac_driver.cpp
│       │   └── jni_bridge.cpp         # High-speed JNI bridge
│       └── java/com/mornye/usbaudio/
│           ├── UsbAudioEngine.kt      # Android UsbManager host controller
│           └── UsbAudioPlugin.kt      # Flutter Platform Channel bridge
└── flutter_plugin/
    ├── pubspec.yaml
    └── lib/
        ├── mornye_usb_audio.dart      # Dart API
        └── mornye_usb_sink.dart       # Mornye AudioSink drop-in adapter
```

---

## 4. How to Integrate into Mornye (`mornyeapp/Mornye`)

### Step 1: Add Dependency to `pubspec.yaml`
In your Mornye project root:
```yaml
dependencies:
  flutter:
    sdk: flutter
  mornye_usb_audio:
    path: ./mornye-usb-audio/flutter_plugin
```

### Step 2: Add USB Host Feature to `android/app/src/main/AndroidManifest.xml`
```xml
<manifest ...>
    <uses-feature android:name="android.hardware.usb.host" android:required="false" />
    <application ...>
        ...
    </application>
</manifest>
```

### Step 3: Use `MornyeUsbSink` in Mornye Playback Pipeline
In Mornye's audio engine:
```dart
import 'package:mornye_usb_audio/mornye_usb_sink.dart';

final usbSink = MornyeUsbSink();

// When a track starts:
bool connected = await usbSink.initialize();
if (connected) {
  // Lock DAC to the track's native format (e.g. 24-bit / 48,000 Hz)
  await usbSink.prepare(sampleRate: 48000, bitDepth: 24, channels: 2);

  // In your audio decoding loop (FLAC/ALAC/PCM):
  decoder.onAudioFrame = (Uint8List pcmBytes) {
    usbSink.feedPcm(pcmBytes);
  };
} else {
  // Fallback to standard player if no USB DAC is connected
}

// Adjust hardware DAC volume directly (bypasses software digital scaling):
await usbSink.setVolume(85); // 0 to 100%
```

---

## 5. Key Features

- **True Bit-Perfect Output:** Never resamples 44.1 kHz to 48 kHz, never resamples 24-bit to float, never passes through vendor Dirac DSP.
- **Lock-Free Audio Ring Buffer:** Zero mutex lock contention between the audio decoder and the real-time USB transfer thread.
- **Hardware Sample Rate Locking:** Supports UAC 1.0 endpoint frequency control and UAC 2.0 Clock Source entities.
- **Hardware Volume & Mute:** Adjusts DAC internal analog/digital gain registers via USB Control transfers.
- **Hot-Plug Support:** Automatically detects DAC attachment and detachment via Android broadcast events.
- **No Root Required:** Works on all Android 8.0+ devices using standard Android USB Host API permissions.
