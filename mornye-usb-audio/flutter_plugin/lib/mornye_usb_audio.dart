import 'dart:async';
import 'dart:convert';
import 'dart:typed_data';
import 'package:flutter/services.dart';

/// Event emitted when USB audio hardware status changes.
class UsbAudioEvent {
  final String type; // 'device_attached', 'device_detached', 'permission_result'
  final String? deviceName;
  final int? vendorId;
  final int? productId;
  final bool? granted;

  UsbAudioEvent({
    required this.type,
    this.deviceName,
    this.vendorId,
    this.productId,
    this.granted,
  });

  factory UsbAudioEvent.fromMap(Map<dynamic, dynamic> map) {
    return UsbAudioEvent(
      type: map['event'] as String? ?? 'unknown',
      deviceName: map['name'] as String?,
      vendorId: map['vendor_id'] as int?,
      productId: map['product_id'] as int?,
      granted: map['granted'] as bool?,
    );
  }
}

/// Bit-Perfect USB Audio Class (UAC1/UAC2) Driver Plugin for Mornye
/// Directly communicates with USB DACs (e.g. Apple USB-C EarPods) bypassing
/// Android's AudioFlinger, 48kHz forced resampler, and Dirac soundfx DSP.
class MornyeUsbAudio {
  static const MethodChannel _channel = MethodChannel('com.mornye.usbaudio/methods');
  static const EventChannel _eventChannel = EventChannel('com.mornye.usbaudio/events');

  Stream<UsbAudioEvent>? _eventsStream;

  /// Listen for USB device attach/detach and permission events.
  Stream<UsbAudioEvent> get events {
    _eventsStream ??= _eventChannel
        .receiveBroadcastStream()
        .map((dynamic event) => UsbAudioEvent.fromMap(event as Map<dynamic, dynamic>));
    return _eventsStream!;
  }

  /// Checks if a compatible USB Audio Class device is currently connected.
  Future<bool> hasUsbAudioDevice() async {
    final bool? result = await _channel.invokeMethod<bool>('hasUsbAudioDevice');
    return result ?? false;
  }

  /// Returns the human-readable product name of the connected USB DAC.
  Future<String?> getConnectedDeviceName() async {
    return await _channel.invokeMethod<String>('getConnectedDeviceName');
  }

  /// Requests Android USB Host permission for the connected DAC.
  Future<bool> requestPermission() async {
    final bool? result = await _channel.invokeMethod<bool>('requestPermission');
    return result ?? false;
  }

  /// Opens the USB device and binds the native Linux UAC driver to its file descriptor.
  Future<bool> openDevice() async {
    final bool? result = await _channel.invokeMethod<bool>('openDevice');
    return result ?? false;
  }

  /// Queries the DAC's hardware capabilities (supported bit depths, sample rates, UAC version).
  Future<Map<String, dynamic>> getCapabilities() async {
    final String? jsonStr = await _channel.invokeMethod<String>('getCapabilities');
    if (jsonStr == null || jsonStr.isEmpty) return {};
    try {
      return jsonDecode(jsonStr) as Map<String, dynamic>;
    } catch (_) {
      return {};
    }
  }

  /// Starts the asynchronous Isochronous USB audio streaming pipeline.
  /// Locks the DAC hardware clock to [sampleRate] and sets container to [bitDepth].
  Future<bool> startStream({
    int sampleRate = 48000,
    int bitDepth = 24,
    int channels = 2,
    int bufferSize = 131072,
  }) async {
    final bool? result = await _channel.invokeMethod<bool>('startStream', {
      'sampleRate': sampleRate,
      'bitDepth': bitDepth,
      'channels': channels,
      'bufferSize': bufferSize,
    });
    return result ?? false;
  }

  /// Pushes decoded raw PCM audio bytes into the lock-free ring buffer.
  Future<int> writePcm(Uint8List pcmBytes) async {
    final int? written = await _channel.invokeMethod<int>('writePcm', {
      'data': pcmBytes,
    });
    return written ?? 0;
  }

  /// Controls the DAC's internal hardware analog/digital gain directly over USB.
  /// [percent] must be between 0 (Mute) and 100 (0 dB Full Scale).
  Future<bool> setHardwareVolume(int percent) async {
    final bool? result = await _channel.invokeMethod<bool>('setHardwareVolume', {
      'percent': percent.clamp(0, 100),
    });
    return result ?? false;
  }

  /// Stops streaming and reverts the USB interface to AltSetting 0 (idle).
  Future<void> stopStream() async {
    await _channel.invokeMethod('stopStream');
  }

  /// Releases the USB interface and closes the connection.
  Future<void> closeDevice() async {
    await _channel.invokeMethod('closeDevice');
  }
}
