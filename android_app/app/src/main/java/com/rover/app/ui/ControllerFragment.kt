package com.rover.app.ui

import android.os.Bundle
import android.view.*
import android.widget.*
import androidx.fragment.app.Fragment
import androidx.fragment.app.activityViewModels
import androidx.lifecycle.lifecycleScope
import androidx.navigation.fragment.findNavController
import com.rover.app.R
import com.rover.app.RoverViewModel
import kotlinx.coroutines.launch
import kotlin.math.abs

// ============================================================
//  ControllerFragment — driving UI
//  Left joystick:  Y = forward/back,  X = unused (or strafe)
//  Right joystick: X = rotation
//  ESTOP button, Horn button, nav to Cameras
// ============================================================
class ControllerFragment : Fragment() {

    private val vm: RoverViewModel by activityViewModels()

    private lateinit var leftJoystick:  JoystickView
    private lateinit var rightJoystick: JoystickView
    private lateinit var estopBtn:      Button
    private lateinit var hornBtn:       Button
    private lateinit var camBtn:        Button
    private lateinit var otaBtn:        Button
    private lateinit var disconnectBtn: Button
    private lateinit var voltText:      TextView
    private lateinit var currText:      TextView
    private lateinit var tempText:      TextView
    private lateinit var encText:       TextView
    private lateinit var transportText: TextView

    // Current joystick values
    private var leftY   = 0f   // forward/back
    private var rightX  = 0f   // rotation

    override fun onCreateView(
        inflater: LayoutInflater, container: ViewGroup?, saved: Bundle?
    ): View = inflater.inflate(R.layout.fragment_controller, container, false)

    override fun onViewCreated(view: View, saved: Bundle?) {
        leftJoystick  = view.findViewById(R.id.joystickLeft)
        rightJoystick = view.findViewById(R.id.joystickRight)
        estopBtn      = view.findViewById(R.id.btnEstop)
        hornBtn       = view.findViewById(R.id.btnHorn)
        camBtn        = view.findViewById(R.id.btnCameras)
        otaBtn        = view.findViewById(R.id.btnOta)
        disconnectBtn = view.findViewById(R.id.btnDisconnect)
        voltText      = view.findViewById(R.id.txtVolt)
        currText      = view.findViewById(R.id.txtCurr)
        tempText      = view.findViewById(R.id.txtTemp)
        encText       = view.findViewById(R.id.txtEnc)
        transportText = view.findViewById(R.id.txtTransport)

        // ---- Joystick callbacks --------------------------------
        leftJoystick.onMove = { _, y ->
            leftY = y
            sendDrive()
        }
        rightJoystick.onMove = { x, _ ->
            rightX = x
            sendDrive()
        }

        // ---- Buttons -------------------------------------------
        estopBtn.setOnClickListener { vm.sendEstop() }
        hornBtn.setOnClickListener  { vm.sendHorn() }
        camBtn.setOnClickListener   {
            findNavController().navigate(R.id.action_controller_to_cameras)
        }
        otaBtn.setOnClickListener   {
            findNavController().navigate(R.id.action_controller_to_ota)
        }
        disconnectBtn.setOnClickListener {
            vm.ble.disconnect()
            vm.wifi.disconnect()
            findNavController().navigateUp()
        }

        // ---- Observe telemetry ---------------------------------
        lifecycleScope.launch {
            vm.telemetry.collect { t ->
                voltText.text = "%.1f V".format(t.voltage)
                currText.text = "%.2f A".format(t.current)
                tempText.text = "%.0f°C".format(t.temp)
                encText.text  = "L:${t.encL}  R:${t.encR}"
            }
        }

        // ---- Observe connection --------------------------------
        lifecycleScope.launch {
            vm.connection.collect { state ->
                transportText.text = if (state.connected)
                    "${state.transport} ▪ ${state.deviceName.ifEmpty { state.ip }}"
                else
                    "Disconnected"
                if (!state.connected) findNavController().navigateUp()
            }
        }
    }

    private fun sendDrive() {
        // Deadband handled server-side too, but also apply here
        val fwd = if (abs(leftY)  < 0.04f) 0f else leftY
        val rot = if (abs(rightX) < 0.04f) 0f else rightX
        vm.sendDrive(x = 0f, y = fwd, rot = rot)
    }

    override fun onPause() {
        super.onPause()
        vm.sendEstop()   // safety: stop when app goes to background
    }
}
