package org.lineageos.pcterminal.ui

import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.offset
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.IntOffset
import kotlin.math.roundToInt
import org.lineageos.pcterminal.R

/** One entry of the right button's menu, with its keys shown on the right. */
private class MenuEntry(val label: Int, val keys: Int, val isEnabled: (TerminalViewState) -> Boolean, val run: (TerminalViewState) -> Unit)

private val MENU_ENTRIES = listOf(
    MenuEntry(R.string.menu_copy, R.string.keys_copy, TerminalViewState::hasSelection, TerminalViewState::copy),
    MenuEntry(R.string.menu_paste, R.string.keys_paste, TerminalViewState::hasClipboardText, TerminalViewState::paste),
    MenuEntry(R.string.menu_select_all, R.string.keys_select_all, { true }, TerminalViewState::selectAll),
)

/** The menu the right button opens where it was pressed: copy, paste, select all. [onClose] after it closes. */
@Composable
fun TerminalMenu(state: TerminalViewState, onClose: () -> Unit) {
    val position = state.menuPosition ?: return
    val close = {
        state.menuPosition = null
        onClose()
    }
    Box(Modifier.offset { IntOffset(position.x.roundToInt(), position.y.roundToInt()) }) {
        DropdownMenu(expanded = true, onDismissRequest = close) {
            MENU_ENTRIES.forEach { entry ->
                DropdownMenuItem(
                    text = { Text(stringResource(entry.label)) },
                    trailingIcon = {
                        Text(stringResource(entry.keys), style = MaterialTheme.typography.labelMedium, color = MaterialTheme.colorScheme.onSurfaceVariant)
                    },
                    enabled = entry.isEnabled(state),
                    onClick = {
                        entry.run(state)
                        close()
                    },
                )
            }
        }
    }
}
