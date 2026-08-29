package com.ehb.handbell

import android.annotation.SuppressLint
import android.bluetooth.BluetoothDevice
import android.bluetooth.BluetoothGatt
import android.bluetooth.BluetoothGattCallback
import android.bluetooth.BluetoothGattCharacteristic
import android.bluetooth.BluetoothGattDescriptor
import android.bluetooth.BluetoothManager
import android.bluetooth.BluetoothProfile
import android.bluetooth.le.ScanCallback
import android.bluetooth.le.ScanFilter
import android.bluetooth.le.ScanResult
import android.bluetooth.le.ScanSettings
import android.content.Context
import android.os.Handler
import android.os.Looper
import android.util.Log
import java.util.UUID

/**
 * Scans for the "WirelessHandbell" BLE peripheral, connects, subscribes to ring
 * notifications, and auto-reconnects on disconnect. Requests the fastest
 * connection priority Android exposes to an app, to keep notify latency down —
 * see the matching NimBLEServer::updateConnParams() call in feather_transmitter.ino.
 *
 * Caller must hold BLUETOOTH_SCAN/BLUETOOTH_CONNECT before calling start().
 */
class BleRingClient(
    private val context: Context,
    private val onStatus: (String) -> Unit,
    private val onRing: (RingEvent) -> Unit,
    private val onBattery: (BatteryStatus) -> Unit,
) {
    private companion object {
        const val TAG = "BleRingClient"
        const val DEVICE_NAME = "WirelessHandbell"
        val SERVICE_UUID: UUID = UUID.fromString("6e400001-b5a3-f393-e0a9-e50e24dcca9e")
        val CHAR_RING_UUID: UUID = UUID.fromString("6e400002-b5a3-f393-e0a9-e50e24dcca9e")
        val CHAR_BATTERY_UUID: UUID = UUID.fromString("6e400003-b5a3-f393-e0a9-e50e24dcca9e")
        val CCCD_UUID: UUID = UUID.fromString("00002902-0000-1000-8000-00805f9b34fb")
        const val RESCAN_DELAY_MS = 1000L
    }

    private val bluetoothManager =
        context.getSystemService(Context.BLUETOOTH_SERVICE) as BluetoothManager
    private val mainHandler = Handler(Looper.getMainLooper())

    private var gatt: BluetoothGatt? = null
    private var scanning = false
    private var stopped = false

    @SuppressLint("MissingPermission")
    fun start() {
        stopped = false
        val adapter = bluetoothManager.adapter
        if (adapter == null || !adapter.isEnabled) {
            onStatus("Bluetooth is off")
            return
        }
        startScan()
    }

    @SuppressLint("MissingPermission")
    fun stop() {
        stopped = true
        stopScanInternal()
        gatt?.close()
        gatt = null
    }

    @SuppressLint("MissingPermission")
    private fun startScan() {
        if (stopped || scanning) return
        val scanner = bluetoothManager.adapter?.bluetoothLeScanner
        if (scanner == null) {
            onStatus("No BLE scanner available")
            return
        }
        val filter = ScanFilter.Builder().setDeviceName(DEVICE_NAME).build()
        val settings = ScanSettings.Builder()
            .setScanMode(ScanSettings.SCAN_MODE_LOW_LATENCY)
            .build()
        scanning = true
        onStatus("Scanning for $DEVICE_NAME…")
        scanner.startScan(listOf(filter), settings, scanCallback)
    }

    @SuppressLint("MissingPermission")
    private fun stopScanInternal() {
        if (!scanning) return
        scanning = false
        bluetoothManager.adapter?.bluetoothLeScanner?.stopScan(scanCallback)
    }

    private val scanCallback = object : ScanCallback() {
        @SuppressLint("MissingPermission")
        override fun onScanResult(callbackType: Int, result: ScanResult) {
            stopScanInternal()
            val device = result.device
            onStatus("Found ${device.name ?: device.address}, connecting…")
            gatt = device.connectGatt(context, false, gattCallback, BluetoothDevice.TRANSPORT_LE)
        }

        override fun onScanFailed(errorCode: Int) {
            scanning = false
            onStatus("Scan failed ($errorCode), retrying…")
            mainHandler.postDelayed({ startScan() }, RESCAN_DELAY_MS)
        }
    }

    private val gattCallback = object : BluetoothGattCallback() {
        @SuppressLint("MissingPermission")
        override fun onConnectionStateChange(g: BluetoothGatt, status: Int, newState: Int) {
            when (newState) {
                BluetoothProfile.STATE_CONNECTED -> {
                    onStatus("Connected — discovering services…")
                    // Request the lowest-latency connection interval Android's public API
                    // allows; the firmware requests an even tighter one server-side too.
                    g.requestConnectionPriority(BluetoothGatt.CONNECTION_PRIORITY_HIGH)
                    g.discoverServices()
                }
                BluetoothProfile.STATE_DISCONNECTED -> {
                    onStatus("Disconnected…")
                    g.close()
                    if (gatt === g) gatt = null
                    if (!stopped) mainHandler.postDelayed({ startScan() }, RESCAN_DELAY_MS)
                }
            }
        }

        // Android's GATT stack allows only one outstanding read/write at a time —
        // issuing a second descriptor write before the first one's callback fires
        // silently fails. Queue them and drain one at a time from onDescriptorWrite.
        private val descriptorWriteQueue = ArrayDeque<BluetoothGattDescriptor>()

        @SuppressLint("MissingPermission")
        override fun onServicesDiscovered(g: BluetoothGatt, status: Int) {
            if (status != BluetoothGatt.GATT_SUCCESS) {
                Log.w(TAG, "Service discovery failed: $status")
                onStatus("Service discovery failed")
                return
            }
            val service = g.getService(SERVICE_UUID)
            val ringOk = enableNotify(g, service?.getCharacteristic(CHAR_RING_UUID))
            if (!ringOk) {
                onStatus("Ring characteristic not found")
                return
            }
            // Battery telemetry is a nice-to-have — don't fail the connection over it.
            val batteryChar = service?.getCharacteristic(CHAR_BATTERY_UUID)
            if (batteryChar == null || !enableNotify(g, batteryChar)) {
                Log.w(TAG, "Battery characteristic not found")
            }
            onStatus("Connected to $DEVICE_NAME")
        }

        @SuppressLint("MissingPermission")
        private fun enableNotify(g: BluetoothGatt, characteristic: BluetoothGattCharacteristic?): Boolean {
            if (characteristic == null) return false
            g.setCharacteristicNotification(characteristic, true)
            val descriptor = characteristic.getDescriptor(CCCD_UUID) ?: return false
            @Suppress("DEPRECATION")
            descriptor.value = BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE
            val wasIdle = descriptorWriteQueue.isEmpty()
            descriptorWriteQueue.addLast(descriptor)
            if (wasIdle) {
                @Suppress("DEPRECATION")
                g.writeDescriptor(descriptor)
            }
            return true
        }

        @SuppressLint("MissingPermission")
        @Suppress("DEPRECATION")
        override fun onDescriptorWrite(g: BluetoothGatt, descriptor: BluetoothGattDescriptor, status: Int) {
            if (status != BluetoothGatt.GATT_SUCCESS) {
                Log.w(TAG, "Descriptor write failed for ${descriptor.characteristic.uuid}: status=$status")
            }
            descriptorWriteQueue.removeFirstOrNull()
            val next = descriptorWriteQueue.firstOrNull() ?: return
            g.writeDescriptor(next)
        }

        // Deprecated in API 33, but still the callback the platform actually invokes
        // regardless of API level — the API-33 overload with an explicit byte[] param
        // is an additional opt-in hook, not a replacement. Simplest to just use this one.
        @Suppress("DEPRECATION")
        override fun onCharacteristicChanged(
            g: BluetoothGatt,
            characteristic: BluetoothGattCharacteristic,
        ) {
            val value = characteristic.value ?: return
            when (characteristic.uuid) {
                CHAR_RING_UUID -> RingEvent.parse(value)?.let(onRing)
                CHAR_BATTERY_UUID -> BatteryStatus.parse(value)?.let(onBattery)
            }
        }
    }
}
