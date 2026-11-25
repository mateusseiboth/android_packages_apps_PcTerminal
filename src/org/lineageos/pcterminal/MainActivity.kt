package org.lineageos.pcterminal

import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.lifecycle.ViewModelProvider
import org.lineageos.pcterminal.ui.PcTerminalTheme
import org.lineageos.pcterminal.ui.TerminalApp

class MainActivity : ComponentActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        val viewModel = ViewModelProvider(this)[TerminalViewModel::class.java]
        setContent {
            PcTerminalTheme {
                TerminalApp(viewModel)
            }
        }
    }
}
