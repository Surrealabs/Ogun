package com.rover.app.ui

import android.app.Activity
import android.content.Intent
import android.os.Bundle
import android.view.*
import android.widget.*
import androidx.activity.result.contract.ActivityResultContracts
import androidx.fragment.app.Fragment
import androidx.fragment.app.activityViewModels
import androidx.lifecycle.lifecycleScope
import com.rover.app.R
import com.rover.app.RoverViewModel
import kotlinx.coroutines.launch

// ============================================================
//  OtaFragment — upload a .hex file to the Teensy
// ============================================================
class OtaFragment : Fragment() {

    private val vm: RoverViewModel by activityViewModels()

    private lateinit var pickBtn:    Button
    private lateinit var flashBtn:   Button
    private lateinit var progressBar: ProgressBar
    private lateinit var statusText: TextView
    private lateinit var fileText:   TextView

    private var hexBytes: ByteArray? = null

    private val pickLauncher = registerForActivityResult(
        ActivityResultContracts.StartActivityForResult()
    ) { result ->
        if (result.resultCode == Activity.RESULT_OK) {
            val uri = result.data?.data ?: return@registerForActivityResult
            hexBytes = requireContext().contentResolver
                .openInputStream(uri)?.use { it.readBytes() }
            fileText.text = uri.lastPathSegment ?: "firmware.hex"
            flashBtn.isEnabled = hexBytes != null
            statusText.text = "${hexBytes?.size ?: 0} bytes loaded"
        }
    }

    override fun onCreateView(
        inflater: LayoutInflater, container: ViewGroup?, saved: Bundle?
    ): View = inflater.inflate(R.layout.fragment_ota, container, false)

    override fun onViewCreated(view: View, saved: Bundle?) {
        pickBtn    = view.findViewById(R.id.btnPickFile)
        flashBtn   = view.findViewById(R.id.btnFlash)
        progressBar = view.findViewById(R.id.otaProgress)
        statusText  = view.findViewById(R.id.otaStatus)
        fileText    = view.findViewById(R.id.otaFileName)

        flashBtn.isEnabled = false

        pickBtn.setOnClickListener {
            val intent = Intent(Intent.ACTION_GET_CONTENT).apply {
                type = "*/*"
                addCategory(Intent.CATEGORY_OPENABLE)
            }
            pickLauncher.launch(intent)
        }

        flashBtn.setOnClickListener {
            val bytes = hexBytes ?: return@setOnClickListener
            flashBtn.isEnabled = false
            pickBtn.isEnabled  = false
            statusText.text    = "Uploading..."
            progressBar.progress = 0
            vm.uploadFirmware(bytes)
        }

        // Observe OTA progress updates
        lifecycleScope.launch {
            vm.otaProgress.collect { prog ->
                prog ?: return@collect
                progressBar.progress = prog.percent
                statusText.text = "${prog.percent}% — ${prog.message}"
                if (prog.percent == 100 || prog.percent == 0) {
                    flashBtn.isEnabled = (hexBytes != null)
                    pickBtn.isEnabled  = true
                }
            }
        }
    }
}
