import 'dart:async';
import 'dart:typed_data';
import 'mornye_usb_audio.dart';

/// Ready-to-use AudioSink adapter for Mornye.
/// Automatically handles DAC connection, sample rate switching, and bit-perfect streaming.
class MornyeUsbSink {
  final MornyeUsbAudio _usbAudio = MornyeUsbAudio();
  bool _isStreaming = false;
  int _currentSampleRate = 0;
  int _currentBitDepth = 0;
  int _currentChannels = 0;

  bool get isStreaming => _isStreaming;
  int get currentSampleRate => _currentSampleRate;
  int get currentBitDepth => _currentBitDepth;

  /// Initializes USB Audio subsystem and attempts to open connected DAC.
  Future<bool> initialize() async {
    final hasDevice = await _usbAudio.hasUsbAudioDevice();
    if (!hasDevice) return false;

    final hasPerm = await _usbAudio.requestPermission();
    if (!hasPerm) return false;

    return await _usbAudio.openDevice();
  }

  /// Starts bit-perfect playback configured to match the audio track's exact parameters.
  Future<bool> prepare({
    required int sampleRate,
    required int bitDepth,
    required int channels,
  }) async {
    if (_isStreaming) {
      await stop();
    }

    final success = await _usbAudio.startStream(
      sampleRate: sampleRate,
      bitDepth: bitDepth,
      channels: channels,
    );

    if (success) {
      _isStreaming = true;
      _currentSampleRate = sampleRate;
      _currentBitDepth = bitDepth;
      _currentChannels = channels;
    }

    return success;
  }

  /// Feeds decoded PCM frames from Mornye's audio decoder.
  Future<int> feedPcm(Uint8List pcmData) async {
    if (!_isStreaming) return 0;
    return await _usbAudio.writePcm(pcmData);
  }

  /// Sets DAC hardware volume (0-100%).
  Future<bool> setVolume(int percent) async {
    return await _usbAudio.setHardwareVolume(percent);
  }

  /// Stops current playback stream.
  Future<void> stop() async {
    if (_isStreaming) {
      await _usbAudio.stopStream();
      _isStreaming = false;
    }
  }

  /// Cleans up USB resources.
  Future<void> dispose() async {
    await stop();
    await _usbAudio.closeDevice();
  }
}
