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
import android.os.SystemClock
import android.util.Log
import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.util.UUID

/**
 * Scans for the "WirelessHandbell" BLE peripheral, connects, subscribes to ring
 * and battery notifications, and auto-reconnects on disconnect. Requests the
 * fastest connection priority Android exposes to an app, to keep notify
 * latency down — see the matching NimBLEServer::updateConnParams() call in
 * feather_transmitter.ino.
 *
 * Also estimates the offset between the Feather's clock (millis(), used in
 * RingEvent.timestampMs) and the phone's own clock (SystemClock.elapsedRealtime()),
 * by reading BLE_CHAR_TIME_UUID once after connecting and bracketing the round
 * trip (midpoint method — assumes the read is roughly symmetric, which is a fair
 * assumption given the ~7.5-15ms connection interval the firmware requests).
 * This is what lets onRing() report true end-to-end ring-to-tone latency rather
 * than just "time since this app received the BLE notification."
 *
 * Caller must hold BLUETOOTH_SCAN/BLUETOOTH_CONNECT before calling start().
 */
class BleRingClient(
    private val context: Context,
    private val onStatus: (String) -> Unit,
    private val onBattery: (BatteryStatus) -> Unit,
    /**
     * receivedAtElapsedMs: phone-clock time (SystemClock.elapsedRealtime()) the
     * notification arrived. estimatedDetectionAtElapsedMs: the ring's own
     * detection instant, converted into the phone's clock domain via the sync
     * offset -- null if we haven't completed a clock sync yet.
     */
    private val onRing: (event: RingEvent, receivedAtElapsedMs: Long, estimatedDetectionAtElapsedMs: Long?) -> Unit,
    /** Fired when the firmware detects the damp gesture (motion in any direction
     *  outside the forward ring cone, ended by a sudden stop) -- see
     *  BLE_CHAR_DAMP_UUID in feather_transmitter.ino. No payload needed; it's a
     *  pure "stop whatever is sounding" signal. */
    private val onDamp: () -> Unit,
) {
    private companion object {
        const val TAG = "BleRingClient"
        const val DEVICE_NAME = "WirelessHandbell"
        val SERVICE_UUID: UUID = UUID.fromString("6e400001-b5a3-f393-e0a9-e50e24dcca9e")
        val CHAR_RING_UUID: UUID = UUID.fromString("6e400002-b5a3-f393-e0a9-e50e24dcca9e")
        val CHAR_BATTERY_UUID: UUID = UUID.fromString("6e400003-b5a3-f393-e0a9-e50e24dcca9e")
        val CHAR_TIME_UUID: UUID = UUID.fromString("6e400004-b5a3-f393-e0a9-e50e24dcca9e")
        val CHAR_DAMP_UUID: UUID = UUID.fromString("6e400005-b5a3-f393-e0a9-e50e24dcca9e")
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

        // Android's GATT stack allows only one outstanding operation (read OR
        // write) at a time -- issuing a second one before the first's callback
        // fires silently fails. Queue every op and drain one at a time.
        private val opQueue = ArrayDeque<() -> Unit>()
        private var opInFlight = false

        private fun enqueue(op: () -> Unit) {
            opQueue.addLast(op)
            if (!opInFlight) runNextOp()
        }

        private fun runNextOp() {
            val next = opQueue.removeFirstOrNull()
            if (next == null) {
                opInFlight = false
                return
            }
            opInFlight = true
            next()
        }

        // Set right before issuing the clock-sync read; consumed in onCharacteristicRead.
        private var syncReadStartedAtElapsedMs = 0L

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
                    opQueue.clear()
                    opInFlight = false
                    clockOffsetMs = null
                    if (!stopped) mainHandler.postDelayed({ startScan() }, RESCAN_DELAY_MS)
                }
            }
        }

        @SuppressLint("MissingPermission")
        override fun onServicesDiscovered(g: BluetoothGatt, status: Int) {
            if (status != BluetoothGatt.GATT_SUCCESS) {
                Log.w(TAG, "Service discovery failed: $status")
                onStatus("Service discovery failed")
                return
            }
            val service = g.getService(SERVICE_UUID)

            val ringChar = service?.getCharacteristic(CHAR_RING_UUID)
            if (ringChar == null) {
                onStatus("Ring characteristic not found")
                return
            }

            // Sync first, so an offset is ready as early as possible for the first ring.
            val timeChar = service.getCharacteristic(CHAR_TIME_UUID)
            if (timeChar != null) {
                enqueue {
                    syncReadStartedAtElapsedMs = SystemClock.elapsedRealtime()
                    g.readCharacteristic(timeChar)
                }
            } else {
                Log.w(TAG, "Time characteristic not found — latency won't be measurable")
            }

            enableNotify(g, ringChar)

            // Battery telemetry and damp are both nice-to-haves — don't fail the
            // connection over either.
            val batteryChar = service.getCharacteristic(CHAR_BATTERY_UUID)
            if (batteryChar == null) {
                Log.w(TAG, "Battery characteristic not found")
            } else {
                enableNotify(g, batteryChar)
            }

            val dampChar = service.getCharacteristic(CHAR_DAMP_UUID)
            if (dampChar == null) {
                Log.w(TAG, "Damp characteristic not found")
            } else {
                enableNotify(g, dampChar)
            }

            onStatus("Connected to $DEVICE_NAME")
        }

        @SuppressLint("MissingPermission")
        private fun enableNotify(g: BluetoothGatt, characteristic: BluetoothGattCharacteristic) {
            g.setCharacteristicNotification(characteristic, true)
            val descriptor = characteristic.getDescriptor(CCCD_UUID)
            if (descriptor == null) {
                Log.w(TAG, "No CCCD on ${characteristic.uuid}")
                return
            }
            @Suppress("DEPRECATION")
            descriptor.value = BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE
            enqueue {
                @Suppress("DEPRECATION")
                g.writeDescriptor(descriptor)
            }
        }

        @SuppressLint("MissingPermission")
        @Suppress("DEPRECATION")
        override fun onDescriptorWrite(g: BluetoothGatt, descriptor: BluetoothGattDescriptor, status: Int) {
            if (status != BluetoothGatt.GATT_SUCCESS) {
                Log.w(TAG, "Descriptor write failed for ${descriptor.characteristic.uuid}: status=$status")
            }
            runNextOp()
        }

        // Deprecated in API 33, but still the callback the platform actually invokes
        // regardless of API level -- the API-33 overload with an explicit byte[] param
        // is an additional opt-in hook, not a replacement. Simplest to use this one
        // consistently, same as onCharacteristicChanged below.
        @SuppressLint("MissingPermission")
        @Suppress("DEPRECATION")
        override fun onCharacteristicRead(
            g: BluetoothGatt,
            characteristic: BluetoothGattCharacteristic,
            status: Int,
        ) {
            val readFinishedAt = SystemClock.elapsedRealtime()
            if (status == BluetoothGatt.GATT_SUCCESS && characteristic.uuid == CHAR_TIME_UUID) {
                val value = characteristic.value
                if (value != null && value.size >= 4) {
                    val firmwareMillis = ByteBuffer.wrap(value).order(ByteOrder.LITTLE_ENDIAN).int.toLong() and 0xFFFFFFFFL
                    val roundTripMs = readFinishedAt - syncReadStartedAtElapsedMs
                    val midpointPhoneTime = syncReadStartedAtElapsedMs + roundTripMs / 2
                    clockOffsetMs = midpointPhoneTime - firmwareMillis
                    Log.i(TAG, "Clock sync: offset=${clockOffsetMs}ms roundTrip=${roundTripMs}ms")
                }
            } else {
                Log.w(TAG, "Characteristic read failed for ${characteristic.uuid}: status=$status")
            }
            runNextOp()
        }

        // Same deprecation note as onCharacteristicRead above.
        @Suppress("DEPRECATION")
        override fun onCharacteristicChanged(
            g: BluetoothGatt,
            characteristic: BluetoothGattCharacteristic,
        ) {
            // Capture this first, before any parsing -- it's the "arrived" instant.
            val receivedAt = SystemClock.elapsedRealtime()
            val value = characteristic.value ?: return
            when (characteristic.uuid) {
                CHAR_RING_UUID -> {
                    val event = RingEvent.parse(value) ?: return
                    val offset = clockOffsetMs
                    val estimatedDetectionAt = if (offset != null) event.timestampMs + offset else null
                    onRing(event, receivedAt, estimatedDetectionAt)
                }
                CHAR_BATTERY_UUID -> BatteryStatus.parse(value)?.let(onBattery)
                CHAR_DAMP_UUID -> onDamp()
            }
        }
    }

    /** Phone-clock (elapsedRealtime) minus Feather-clock (millis()) offset, once synced. */
    @Volatile
    private var clockOffsetMs: Long? = null
}
