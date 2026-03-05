package com.rover.app.ble

import android.annotation.SuppressLint
import android.bluetooth.*
import android.bluetooth.le.*
import android.content.Context
import android.util.Log
import kotlinx.coroutines.channels.Channel
import kotlinx.coroutines.flow.MutableSharedFlow
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.asSharedFlow
import kotlinx.coroutines.flow.asStateFlow
import java.util.UUID

private const val TAG = "BleManager"

@SuppressLint("MissingPermission")
class BleManager(private val context: Context) {

    private val adapter: BluetoothAdapter? =
        (context.getSystemService(Context.BLUETOOTH_SERVICE) as BluetoothManager).adapter

    // Observable state
    private val _connectionState = MutableStateFlow(Pair(false, ""))
    val connectionState = _connectionState.asStateFlow()

    private val _incoming = MutableSharedFlow<String>(extraBufferCapacity = 64)
    val incoming = _incoming.asSharedFlow()

    private var gatt: BluetoothGatt?  = null
    private var controlChar: BluetoothGattCharacteristic? = null
    private var statusChar:  BluetoothGattCharacteristic? = null
    private var otaChar:     BluetoothGattCharacteristic? = null

    // Serialize writes through a channel (BLE needs sequential writes)
    private val writeQueue = Channel<ByteArray>(capacity = 128)

    private val serviceUUID = UUID.fromString("0000ABCD-0000-1000-8000-00805F9B34FB")
    private val ctrlUUID    = UUID.fromString("0000ABD0-0000-1000-8000-00805F9B34FB")
    private val statusUUID  = UUID.fromString("0000ABD1-0000-1000-8000-00805F9B34FB")
    private val otaUUID     = UUID.fromString("0000ABD2-0000-1000-8000-00805F9B34FB")
    private val cccdUUID    = UUID.fromString("00002902-0000-1000-8000-00805F9B34FB")

    // ---- BLE Scanner ----------------------------------------
    fun startScan(onFound: (BluetoothDevice) -> Unit) {
        val scanner = adapter?.bluetoothLeScanner ?: return
        val filter  = ScanFilter.Builder()
            .setServiceUuid(android.os.ParcelUuid.fromString(serviceUUID.toString()))
            .build()
        val settings = ScanSettings.Builder()
            .setScanMode(ScanSettings.SCAN_MODE_LOW_LATENCY)
            .build()
        scanner.startScan(listOf(filter), settings, object : ScanCallback() {
            override fun onScanResult(callbackType: Int, result: ScanResult) {
                scanner.stopScan(this)
                onFound(result.device)
            }
        })
    }

    // ---- Connect to a specific device ----------------------
    fun connect(device: BluetoothDevice) {
        Log.d(TAG, "Connecting to ${device.name ?: device.address}")
        gatt = device.connectGatt(context, false, gattCallback,
                                  BluetoothDevice.TRANSPORT_LE)
    }

    fun disconnect() {
        gatt?.close()
        gatt = null
        controlChar = null
        statusChar  = null
        otaChar     = null
        _connectionState.tryEmit(Pair(false, ""))
    }

    // ---- Send JSON command ----------------------------------
    fun send(json: String) {
        val char = controlChar ?: run {
            Log.w(TAG, "Control characteristic not available")
            return
        }
        val bytes = json.toByteArray(Charsets.UTF_8)
        // Use Write Without Response for low-latency drive commands
        char.writeType = BluetoothGattCharacteristic.WRITE_TYPE_NO_RESPONSE
        char.value = bytes
        gatt?.writeCharacteristic(char)
    }

    // ---- GATT callbacks ------------------------------------
    private val gattCallback = object : BluetoothGattCallback() {
        override fun onConnectionStateChange(g: BluetoothGatt, status: Int, newState: Int) {
            if (newState == BluetoothProfile.STATE_CONNECTED) {
                Log.d(TAG, "Connected — discovering services")
                g.requestMtu(512)
                g.discoverServices()
            } else {
                Log.d(TAG, "Disconnected (status=$status)")
                _connectionState.tryEmit(Pair(false, ""))
            }
        }

        override fun onMtuChanged(g: BluetoothGatt, mtu: Int, status: Int) {
            Log.d(TAG, "MTU=$mtu")
        }

        override fun onServicesDiscovered(g: BluetoothGatt, status: Int) {
            val svc = g.getService(serviceUUID) ?: run {
                Log.e(TAG, "Rover service not found"); return
            }
            controlChar = svc.getCharacteristic(ctrlUUID)
            statusChar  = svc.getCharacteristic(statusUUID)
            otaChar     = svc.getCharacteristic(otaUUID)

            // Subscribe to Status notifications
            statusChar?.let { ch ->
                g.setCharacteristicNotification(ch, true)
                val cccd = ch.getDescriptor(cccdUUID)
                cccd?.value = BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE
                g.writeDescriptor(cccd)
            }

            val name = g.device.name ?: g.device.address
            _connectionState.tryEmit(Pair(true, name))
            Log.d(TAG, "Services ready — rover connected as $name")
        }

        override fun onCharacteristicChanged(
            g: BluetoothGatt, ch: BluetoothGattCharacteristic) {
            val json = ch.value.toString(Charsets.UTF_8)
            _incoming.tryEmit(json)
        }
    }
}
