package com.mornye.usbaudio

import android.app.PendingIntent
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.hardware.usb.UsbConstants
import android.hardware.usb.UsbDevice
import android.hardware.usb.UsbDeviceConnection
import android.hardware.usb.UsbManager
import android.os.Build
import android.util.Log
import java.nio.ByteBuffer

class UsbAudioEngine(private val context: Context) {

    companion object {
        private const val TAG = "UsbAudioEngine"
        private const val ACTION_USB_PERMISSION = "com.mornye.usbaudio.USB_PERMISSION"

        init {
            try {
                System.loadLibrary("mornye_usb_audio")
                Log.i(TAG, "Native library 'mornye_usb_audio' loaded successfully.")
            } catch (e: UnsatisfiedLinkError) {
                Log.e(TAG, "Failed to load native library 'mornye_usb_audio': ${e.message}")
            }
        }
    }

    private val usbManager: UsbManager = context.getSystemService(Context.USB_SERVICE) as UsbManager
    private var activeDevice: UsbDevice? = null
    private var activeConnection: UsbDeviceConnection? = null
    private var isInitialized = false

    var onDeviceAttachedListener: ((UsbDevice) -> Unit)? = null
    var onDeviceDetachedListener: ((UsbDevice) -> Unit)? = null
    var onPermissionResultListener: ((Boolean) -> Unit)? = null

    private val usbReceiver = object : BroadcastReceiver() {
        override fun onReceive(context: Context, intent: Intent) {
            val action = intent.action
            val device: UsbDevice? = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
                intent.getParcelableExtra(UsbManager.EXTRA_DEVICE, UsbDevice::class.java)
            } else {
                @Suppress("DEPRECATION")
                intent.getParcelableExtra(UsbManager.EXTRA_DEVICE)
            }

            when (action) {
                ACTION_USB_PERMISSION -> {
                    val granted = intent.getBooleanExtra(UsbManager.EXTRA_PERMISSION_GRANTED, false)
                    Log.i(TAG, "USB Permission result for ${device?.productName}: $granted")
                    if (granted && device != null) {
                        openDevice(device)
                    }
                    onPermissionResultListener?.invoke(granted)
                }
                UsbManager.ACTION_USB_DEVICE_ATTACHED -> {
                    if (device != null && isAudioDevice(device)) {
                        Log.i(TAG, "USB Audio Device Attached: ${device.productName} (${device.vendorId}:${device.productId})")
                        onDeviceAttachedListener?.invoke(device)
                    }
                }
                UsbManager.ACTION_USB_DEVICE_DETACHED -> {
                    if (device != null && device == activeDevice) {
                        Log.i(TAG, "Active USB Audio Device Detached: ${device.productName}")
                        closeDevice()
                        onDeviceDetachedListener?.invoke(device)
                    }
                }
            }
        }
    }

    init {
        val filter = IntentFilter().apply {
            addAction(ACTION_USB_PERMISSION)
            addAction(UsbManager.ACTION_USB_DEVICE_ATTACHED)
            addAction(UsbManager.ACTION_USB_DEVICE_DETACHED)
        }
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            context.registerReceiver(usbReceiver, filter, Context.RECEIVER_NOT_EXPORTED)
        } else {
            context.registerReceiver(usbReceiver, filter)
        }
    }

    fun isAudioDevice(device: UsbDevice): Boolean {
        if (device.deviceClass == UsbConstants.USB_CLASS_AUDIO) return true
        // Check interface classes for composite devices (like Apple EarPods)
        for (i in 0 until device.interfaceCount) {
            val intf = device.getInterface(i)
            if (intf.interfaceClass == UsbConstants.USB_CLASS_AUDIO) {
                return true
            }
        }
        return false
    }

    fun findConnectedAudioDevice(): UsbDevice? {
        val deviceList = usbManager.deviceList
        for ((_, device) in deviceList) {
            if (isAudioDevice(device)) {
                return device
            }
        }
        return null
    }

    fun requestPermission(device: UsbDevice) {
        if (usbManager.hasPermission(device)) {
            openDevice(device)
            onPermissionResultListener?.invoke(true)
            return
        }

        val flags = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            PendingIntent.FLAG_MUTABLE or PendingIntent.FLAG_UPDATE_CURRENT
        } else {
            PendingIntent.FLAG_UPDATE_CURRENT
        }
        val permissionIntent = PendingIntent.getBroadcast(
            context,
            0,
            Intent(ACTION_USB_PERMISSION),
            flags
        )
        usbManager.requestPermission(device, permissionIntent)
    }

    fun openDevice(device: UsbDevice): Boolean {
        closeDevice()

        if (!usbManager.hasPermission(device)) {
            Log.w(TAG, "No permission to open USB device: ${device.productName}")
            return false
        }

        val connection = usbManager.openDevice(device)
        if (connection == null) {
            Log.e(TAG, "Failed to open USB connection (openDevice returned null)!")
            return false
        }

        val fd = connection.fileDescriptor
        Log.i(TAG, "Obtained Linux USB File Descriptor: $fd for ${device.productName}")

        val success = nativeInit(fd)
        if (success) {
            activeDevice = device
            activeConnection = connection
            isInitialized = true
            Log.i(TAG, "Bit-Perfect USB Engine bound to ${device.productName}!")
            return true
        } else {
            connection.close()
            Log.e(TAG, "nativeInit failed for fd: $fd")
            return false
        }
    }

    fun getCapabilities(): String {
        return if (isInitialized) nativeGetCapabilities() else "[]"
    }

    fun startPlayback(
        sampleRate: Int = 48000,
        bitDepth: Int = 24,
        channels: Int = 2,
        bufferSizeBytes: Int = 131072
    ): Boolean {
        if (!isInitialized) {
            Log.e(TAG, "Cannot start playback: Engine not initialized!")
            return false
        }
        return nativeStart(sampleRate, bitDepth, channels, bufferSizeBytes)
    }

    fun writePcm(data: ByteArray, offset: Int = 0, length: Int = data.size): Int {
        if (!isInitialized) return 0
        return nativeWritePcm(data, offset, length)
    }

    fun writeDirect(buffer: ByteBuffer, length: Int): Int {
        if (!isInitialized) return 0
        return nativeWriteDirect(buffer, length)
    }

    fun setHardwareVolume(percent: Int): Boolean {
        if (!isInitialized) return false
        return nativeSetVolume(percent)
    }

    fun stopPlayback() {
        if (isInitialized) {
            nativeStop()
        }
    }

    fun closeDevice() {
        stopPlayback()
        if (isInitialized) {
            nativeRelease()
            isInitialized = false
        }
        activeConnection?.close()
        activeConnection = null
        activeDevice = null
    }

    fun destroy() {
        try {
            context.unregisterReceiver(usbReceiver)
        } catch (e: Exception) {}
        closeDevice()
    }

    // Native JNI Declarations
    private external fun nativeInit(fd: Int): Boolean
    private external fun nativeGetCapabilities(): String
    private external fun nativeStart(sampleRate: Int, bitDepth: Int, channels: Int, bufferSizeBytes: Int): Boolean
    private external fun nativeWritePcm(data: ByteArray, offset: Int, length: Int): Int
    private external fun nativeWriteDirect(buffer: ByteBuffer, length: Int): Int
    private external fun nativeSetVolume(percent: Int): Boolean
    private external fun nativeStop()
    private external fun nativeRelease()
}
