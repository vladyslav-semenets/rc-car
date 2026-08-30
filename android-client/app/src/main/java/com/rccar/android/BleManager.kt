package com.rccar.android

import android.annotation.SuppressLint
import android.bluetooth.BluetoothAdapter
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
import android.os.ParcelUuid
import android.util.Log
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import java.util.UUID

@SuppressLint("MissingPermission")
class BleManager(private val context: Context) {

    companion object {
        private const val TAG = "BleManager"
        val NUS_SERVICE_UUID: UUID = UUID.fromString("6E400001-B5A3-F393-E0A9-E50E24DCCA9E")
        val NUS_RX_CHAR_UUID: UUID = UUID.fromString("6E400002-B5A3-F393-E0A9-E50E24DCCA9E") // Write from phone
        val NUS_TX_CHAR_UUID: UUID = UUID.fromString("6E400003-B5A3-F393-E0A9-E50E24DCCA9E") // Notify to phone
        val CCCD_UUID: UUID        = UUID.fromString("00002902-0000-1000-8000-00805f9b34fb")
    }

    enum class State {
        DISCONNECTED,
        SCANNING,
        CONNECTING,
        CONNECTED
    }

    var connectionState = mutableStateOf(State.DISCONNECTED)
    var deviceName = mutableStateOf("No Ground TX")
    var rssi = mutableIntStateOf(0)
    var bytesSent = mutableIntStateOf(0)
    var bytesReceived = mutableIntStateOf(0)

    var onTelemetryReceived: ((ByteArray) -> Unit)? = null

    private val bluetoothManager = context.getSystemService(Context.BLUETOOTH_SERVICE) as? BluetoothManager
    private val bluetoothAdapter: BluetoothAdapter? = bluetoothManager?.adapter
    private var bluetoothGatt: BluetoothGatt? = null
    private var rxCharacteristic: BluetoothGattCharacteristic? = null

    private val handler = Handler(Looper.getMainLooper())
    private var isScanning = false

    private val scanCallback = object : ScanCallback() {
        override fun onScanResult(callbackType: Int, result: ScanResult) {
            val device = result.device
            val name = result.scanRecord?.deviceName ?: device.name ?: ""

            if (name.contains("RCCAR", ignoreCase = true) || name.contains("GROUND", ignoreCase = true)) {
                Log.d(TAG, "Found target Ground TX: $name (${device.address})")
                stopScan()
                connectToDevice(device)
            }
        }

        override fun onScanFailed(errorCode: Int) {
            Log.e(TAG, "BLE Scan failed: $errorCode")
            connectionState.value = State.DISCONNECTED
            isScanning = false
        }
    }

    private val gattCallback = object : BluetoothGattCallback() {
        override fun onConnectionStateChange(gatt: BluetoothGatt, status: Int, newState: Int) {
            if (newState == BluetoothProfile.STATE_CONNECTED) {
                Log.d(TAG, "GATT connected, requesting MTU 256...")
                handler.post {
                    connectionState.value = State.CONNECTING
                    deviceName.value = gatt.device.name ?: "Ground TX"
                }
                gatt.requestMtu(256)
            } else if (newState == BluetoothProfile.STATE_DISCONNECTED) {
                Log.d(TAG, "GATT disconnected")
                handler.post {
                    connectionState.value = State.DISCONNECTED
                    deviceName.value = "No Ground TX"
                    rxCharacteristic = null
                }
                bluetoothGatt?.close()
                bluetoothGatt = null
            }
        }

        override fun onMtuChanged(gatt: BluetoothGatt, mtu: Int, status: Int) {
            Log.d(TAG, "MTU changed to $mtu (status: $status), discovering services...")
            gatt.discoverServices()
        }

        override fun onServicesDiscovered(gatt: BluetoothGatt, status: Int) {
            if (status == BluetoothGatt.GATT_SUCCESS) {
                val service = gatt.getService(NUS_SERVICE_UUID)
                if (service != null) {
                    rxCharacteristic = service.getCharacteristic(NUS_RX_CHAR_UUID)
                    val txChar = service.getCharacteristic(NUS_TX_CHAR_UUID)

                    if (txChar != null) {
                        enableNotifications(gatt, txChar)
                    }

                    handler.post {
                        connectionState.value = State.CONNECTED
                        Log.d(TAG, "NUS Service discovered & connected!")
                    }
                } else {
                    Log.e(TAG, "NUS Service not found on device!")
                }
            }
        }

        override fun onCharacteristicChanged(
            gatt: BluetoothGatt,
            characteristic: BluetoothGattCharacteristic
        ) {
            if (characteristic.uuid == NUS_TX_CHAR_UUID) {
                val data = characteristic.value
                if (data != null && data.isNotEmpty()) {
                    handler.post {
                        bytesReceived.intValue += data.size
                        onTelemetryReceived?.invoke(data)
                    }
                }
            }
        }

        override fun onReadRemoteRssi(gatt: BluetoothGatt, rssiVal: Int, status: Int) {
            if (status == BluetoothGatt.GATT_SUCCESS) {
                handler.post { rssi.intValue = rssiVal }
            }
        }
    }

    fun startScan() {
        if (bluetoothAdapter == null || !bluetoothAdapter.isEnabled) {
            Log.w(TAG, "Bluetooth not available or disabled")
            return
        }

        if (isScanning || connectionState.value == State.CONNECTED) return

        val scanner = bluetoothAdapter.bluetoothLeScanner ?: return
        connectionState.value = State.SCANNING
        isScanning = true

        val filters = listOf(
            ScanFilter.Builder()
                .setServiceUuid(ParcelUuid(NUS_SERVICE_UUID))
                .build()
        )
        val settings = ScanSettings.Builder()
            .setScanMode(ScanSettings.SCAN_MODE_LOW_LATENCY)
            .build()

        // Fallback filterless scan to catch by device name
        scanner.startScan(null, settings, scanCallback)

        // Stop scan after 10 seconds if not found
        handler.postDelayed({
            if (isScanning) {
                stopScan()
                if (connectionState.value == State.SCANNING) {
                    connectionState.value = State.DISCONNECTED
                }
            }
        }, 10000)
    }

    fun stopScan() {
        if (isScanning) {
            bluetoothAdapter?.bluetoothLeScanner?.stopScan(scanCallback)
            isScanning = false
        }
    }

    private fun connectToDevice(device: BluetoothDevice) {
        connectionState.value = State.CONNECTING
        bluetoothGatt = device.connectGatt(context, false, gattCallback, BluetoothDevice.TRANSPORT_LE)
    }

    private fun enableNotifications(gatt: BluetoothGatt, characteristic: BluetoothGattCharacteristic) {
        gatt.setCharacteristicNotification(characteristic, true)
        val descriptor = characteristic.getDescriptor(CCCD_UUID)
        if (descriptor != null) {
            descriptor.value = BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE
            gatt.writeDescriptor(descriptor)
        }
    }

    fun sendMavlinkBytes(data: ByteArray) {
        val gatt = bluetoothGatt ?: return
        val rxChar = rxCharacteristic ?: return
        if (connectionState.value != State.CONNECTED) return

        rxChar.writeType = BluetoothGattCharacteristic.WRITE_TYPE_NO_RESPONSE
        rxChar.value = data
        val success = gatt.writeCharacteristic(rxChar)
        if (success) {
            bytesSent.intValue += data.size
        }
    }

    fun disconnect() {
        stopScan()
        bluetoothGatt?.disconnect()
        bluetoothGatt?.close()
        bluetoothGatt = null
        rxCharacteristic = null
        connectionState.value = State.DISCONNECTED
        deviceName.value = "No Ground TX"
    }
}
