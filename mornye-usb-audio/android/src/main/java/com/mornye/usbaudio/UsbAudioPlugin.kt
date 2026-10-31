package com.mornye.usbaudio

import android.content.Context
import androidx.annotation.NonNull
import io.flutter.embedding.engine.plugins.FlutterPlugin
import io.flutter.plugin.common.EventChannel
import io.flutter.plugin.common.MethodCall
import io.flutter.plugin.common.MethodChannel

class UsbAudioPlugin : FlutterPlugin, MethodChannel.MethodCallHandler, EventChannel.StreamHandler {

    private lateinit var channel: MethodChannel
    private lateinit var eventChannel: EventChannel
    private var engine: UsbAudioEngine? = null
    private var eventSink: EventChannel.EventSink? = null

    override fun onAttachedToEngine(@NonNull flutterPluginBinding: FlutterPlugin.FlutterPluginBinding) {
        val context = flutterPluginBinding.applicationContext
        engine = UsbAudioEngine(context)

        channel = MethodChannel(flutterPluginBinding.binaryMessenger, "com.mornye.usbaudio/methods")
        channel.setMethodCallHandler(this)

        eventChannel = EventChannel(flutterPluginBinding.binaryMessenger, "com.mornye.usbaudio/events")
        eventChannel.setStreamHandler(this)

        setupListeners()
    }

    private fun setupListeners() {
        engine?.onDeviceAttachedListener = { device ->
            eventSink?.success(mapOf(
                "event" to "device_attached",
                "name" to (device.productName ?: "USB Audio Device"),
                "vendor_id" to device.vendorId,
                "product_id" to device.productId
            ))
        }

        engine?.onDeviceDetachedListener = { device ->
            eventSink?.success(mapOf(
                "event" to "device_detached",
                "name" to (device.productName ?: "USB Audio Device")
            ))
        }

        engine?.onPermissionResultListener = { granted ->
            eventSink?.success(mapOf(
                "event" to "permission_result",
                "granted" to granted
            ))
        }
    }

    override fun onMethodCall(@NonNull call: MethodCall, @NonNull result: MethodChannel.Result) {
        val uEngine = engine
        if (uEngine == null) {
            result.error("NOT_INITIALIZED", "UsbAudioEngine is null", null)
            return
        }

        when (call.method) {
            "hasUsbAudioDevice" -> {
                val dev = uEngine.findConnectedAudioDevice()
                result.success(dev != null)
            }
            "getConnectedDeviceName" -> {
                val dev = uEngine.findConnectedAudioDevice()
                result.success(dev?.productName ?: dev?.deviceName)
            }
            "requestPermission" -> {
                val dev = uEngine.findConnectedAudioDevice()
                if (dev != null) {
                    uEngine.requestPermission(dev)
                    result.success(true)
                } else {
                    result.error("NO_DEVICE", "No USB Audio device connected", null)
                }
            }
            "openDevice" -> {
                val dev = uEngine.findConnectedAudioDevice()
                if (dev != null) {
                    val opened = uEngine.openDevice(dev)
                    result.success(opened)
                } else {
                    result.success(false)
                }
            }
            "getCapabilities" -> {
                val capsJson = uEngine.getCapabilities()
                result.success(capsJson)
            }
            "startStream" -> {
                val sampleRate = call.argument<Int>("sampleRate") ?: 48000
                val bitDepth = call.argument<Int>("bitDepth") ?: 24
                val channels = call.argument<Int>("channels") ?: 2
                val bufferSize = call.argument<Int>("bufferSize") ?: 131072
                val success = uEngine.startPlayback(sampleRate, bitDepth, channels, bufferSize)
                result.success(success)
            }
            "writePcm" -> {
                val bytes = call.argument<ByteArray>("data")
                if (bytes != null) {
                    val written = uEngine.writePcm(bytes)
                    result.success(written)
                } else {
                    result.success(0)
                }
            }
            "setHardwareVolume" -> {
                val percent = call.argument<Int>("percent") ?: 100
                val ok = uEngine.setHardwareVolume(percent)
                result.success(ok)
            }
            "stopStream" -> {
                uEngine.stopPlayback()
                result.success(null)
            }
            "closeDevice" -> {
                uEngine.closeDevice()
                result.success(null)
            }
            else -> {
                result.notImplemented()
            }
        }
    }

    override fun onListen(arguments: Any?, events: EventChannel.EventSink?) {
        this.eventSink = events
    }

    override fun onCancel(arguments: Any?) {
        this.eventSink = null
    }

    override fun onDetachedFromEngine(@NonNull binding: FlutterPlugin.FlutterPluginBinding) {
        channel.setMethodCallHandler(null)
        eventChannel.setStreamHandler(null)
        engine?.destroy()
        engine = null
    }
}
