package com.rover.app.ui

import android.Manifest
import android.bluetooth.BluetoothDevice
import android.os.Build
import android.os.Bundle
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import android.widget.*
import androidx.core.app.ActivityCompat
import androidx.fragment.app.Fragment
import androidx.fragment.app.activityViewModels
import androidx.lifecycle.lifecycleScope
import androidx.navigation.fragment.findNavController
import com.rover.app.R
import com.rover.app.RoverViewModel
import com.rover.app.Transport
import kotlinx.coroutines.launch

// ============================================================
//  ConnectionFragment — choose BLE scan or WiFi IP entry
// ============================================================
class ConnectionFragment : Fragment() {

    private val vm: RoverViewModel by activityViewModels()

    private lateinit var statusText: TextView
    private lateinit var bleBtn: Button
    private lateinit var wifiBtn: Button
    private lateinit var ipEdit: EditText
    private lateinit var connectWifiBtn: Button
    private lateinit var progressBar: ProgressBar

    override fun onCreateView(
        inflater: LayoutInflater, container: ViewGroup?, saved: Bundle?
    ): View = inflater.inflate(R.layout.fragment_connection, container, false)

    override fun onViewCreated(view: View, saved: Bundle?) {
        statusText    = view.findViewById(R.id.statusText)
        bleBtn        = view.findViewById(R.id.btnBleScan)
        wifiBtn       = view.findViewById(R.id.btnWifiConnect)
        ipEdit        = view.findViewById(R.id.editIp)
        connectWifiBtn = view.findViewById(R.id.btnConnectWifi)
        progressBar   = view.findViewById(R.id.progressBar)

        bleBtn.setOnClickListener { startBleScan() }
        connectWifiBtn.setOnClickListener {
            val ip = ipEdit.text.toString().trim()
            if (ip.isNotEmpty()) {
                vm.wifi.connect(ip)
            } else {
                Toast.makeText(context, "Enter rover IP address", Toast.LENGTH_SHORT).show()
            }
        }

        // Observe connection — navigate to controller when connected
        lifecycleScope.launch {
            vm.connection.collect { state ->
                if (state.connected) {
                    findNavController().navigate(R.id.action_connection_to_controller)
                } else {
                    statusText.text = "Not connected"
                    progressBar.visibility = View.GONE
                }
            }
        }
    }

    private fun startBleScan() {
        // Request permissions on Android 12+
        val perms = if (Build.VERSION.SDK_INT >= 31) {
            arrayOf(Manifest.permission.BLUETOOTH_SCAN, Manifest.permission.BLUETOOTH_CONNECT)
        } else {
            arrayOf(Manifest.permission.ACCESS_FINE_LOCATION)
        }
        ActivityCompat.requestPermissions(requireActivity(), perms, 1)

        statusText.text = "Scanning for rover..."
        progressBar.visibility = View.VISIBLE
        bleBtn.isEnabled = false

        vm.ble.startScan { device: BluetoothDevice ->
            requireActivity().runOnUiThread {
                statusText.text = "Found: ${device.name ?: device.address} — connecting..."
                vm.ble.connect(device)
            }
        }
    }
}
