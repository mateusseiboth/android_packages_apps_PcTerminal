package org.lineageos.pcterminal.ui

import android.content.Context
import androidx.compose.foundation.isSystemInDarkTheme
import androidx.compose.material3.ColorScheme
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.dynamicDarkColorScheme
import androidx.compose.material3.dynamicLightColorScheme
import androidx.compose.runtime.Composable
import androidx.compose.ui.platform.LocalContext

/** The system's wallpaper colors, light or dark with the system setting. */
private val SCHEMES: Map<Boolean, (Context) -> ColorScheme> = mapOf(
    false to ::dynamicLightColorScheme,
    true to ::dynamicDarkColorScheme,
)

@Composable
fun PcTerminalTheme(content: @Composable () -> Unit) {
    val scheme = SCHEMES.getValue(isSystemInDarkTheme())(LocalContext.current)
    MaterialTheme(colorScheme = scheme, content = content)
}
