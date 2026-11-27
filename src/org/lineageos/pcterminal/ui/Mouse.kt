package org.lineageos.pcterminal.ui

import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.input.pointer.PointerButtons
import androidx.compose.ui.input.pointer.PointerEvent
import androidx.compose.ui.input.pointer.PointerEventType
import androidx.compose.ui.input.pointer.PointerInputChange
import androidx.compose.ui.input.pointer.PointerType
import androidx.compose.ui.input.pointer.isAltPressed
import androidx.compose.ui.input.pointer.isCtrlPressed
import androidx.compose.ui.input.pointer.isPrimaryPressed
import androidx.compose.ui.input.pointer.isSecondaryPressed
import androidx.compose.ui.input.pointer.isShiftPressed
import androidx.compose.ui.input.pointer.isTertiaryPressed
import kotlin.math.abs
import kotlin.math.floor
import kotlin.math.roundToInt
import org.lineageos.pcterminal.core.terminal.KeyModifiers
import org.lineageos.pcterminal.core.terminal.MouseAction
import org.lineageos.pcterminal.core.terminal.MouseButton
import org.lineageos.pcterminal.core.terminal.MouseReport
import org.lineageos.pcterminal.core.terminal.MouseTracking
import org.lineageos.pcterminal.core.terminal.SelectionUnit
import org.lineageos.pcterminal.core.terminal.TerminalKey
import org.lineageos.pcterminal.core.terminal.TextPoint
import org.lineageos.pcterminal.core.terminal.TextSelection

private const val SCROLL_LINES = 3

private val BUTTONS: Map<MouseButton, (PointerButtons) -> Boolean> = mapOf(
    MouseButton.LEFT to { it.isPrimaryPressed },
    MouseButton.MIDDLE to { it.isTertiaryPressed },
    MouseButton.RIGHT to { it.isSecondaryPressed },
)

/** A single click selects by characters, a double click by words, a triple click by lines. */
private val CLICK_UNITS = listOf(SelectionUnit.CHARACTER, SelectionUnit.WORD, SelectionUnit.LINE)

/** Drag points fall on the nearest edge between cells; word and line points on the cell under the mouse. */
private val UNIT_COLUMNS: Map<SelectionUnit, (Float, Int) -> Int> = mapOf(
    SelectionUnit.CHARACTER to { x, cols -> x.roundToInt().coerceIn(0, cols) },
    SelectionUnit.WORD to { x, cols -> floor(x).toInt().coerceIn(0, cols - 1) },
    SelectionUnit.LINE to { x, cols -> floor(x).toInt().coerceIn(0, cols - 1) },
)

/** One thing the wheel can do: [use] it when [matches]; lines > 0 is towards the newest output. */
private class WheelUse(val matches: (PointerEvent) -> Boolean, val use: (PointerInputChange, PointerEvent, Int) -> Unit)

/** Where the buttons held now go: the program, or the terminal's own selection, menu and paste. */
private interface MouseTarget {
    fun onPress(button: MouseButton, change: PointerInputChange, event: PointerEvent)
    fun onRelease(button: MouseButton, change: PointerInputChange, event: PointerEvent)
    fun onMove(change: PointerInputChange, event: PointerEvent)
}

/**
 * The mouse in one terminal. While the program asks for mouse reports (vim, htop, mc) its buttons, moves and
 * wheel go to the program, unless Shift is held, as in other terminals. Otherwise the left button selects
 * (double click: words, triple click: lines), the right one opens the menu, the middle one pastes the last
 * selection and the wheel scrolls back (on the alternate screen, where there is no history, it sends arrows).
 */
class MouseHandler(private val state: TerminalViewState, private val metrics: CellMetrics, doubleClickMs: Long) {
    private val session = state.session
    private val clicks = ClickCounter(doubleClickMs)
    private val program = ProgramMouse()
    private val selection = SelectionMouse()
    private var held = emptySet<MouseButton>()
    private var target: MouseTarget = selection

    /** Ordered: the first use of the wheel that matches takes the turn. */
    private val wheels = listOf(
        WheelUse(::isForProgram, ::sendWheel),
        WheelUse({ session.isAlternateScreen }) { _, _, lines -> sendArrows(lines) },
        WheelUse({ true }) { _, _, lines -> state.scrollBy(-lines) },
    )

    fun onPointerEvent(event: PointerEvent) {
        val change = event.changes.firstOrNull() ?: return
        if (event.type == PointerEventType.Scroll) return onWheel(change, event)
        val pressed = findPressed(change, event)
        if (held.isEmpty()) target = if (isForProgram(event)) program else selection
        val previous = held
        held = pressed
        (pressed - previous).forEach { target.onPress(it, change, event) }
        (previous - pressed).forEach { target.onRelease(it, change, event) }
        if (event.type == PointerEventType.Move) target.onMove(change, event)
    }

    /** Mouse buttons by their state; a finger on a touch screen counts as the left button. */
    private fun findPressed(change: PointerInputChange, event: PointerEvent): Set<MouseButton> {
        if (change.type == PointerType.Touch) return if (change.pressed) setOf(MouseButton.LEFT) else emptySet()
        return BUTTONS.filterValues { it(event.buttons) }.keys
    }

    private fun isForProgram(event: PointerEvent): Boolean =
        session.mouseTracking != MouseTracking.NONE && !event.keyboardModifiers.isShiftPressed

    private fun onWheel(change: PointerInputChange, event: PointerEvent) {
        val lines = (change.scrollDelta.y * SCROLL_LINES).roundToInt()
        if (lines == 0) return
        wheels.first { it.matches(event) }.use(change, event, lines)
    }

    /** One wheel button press per notch, down for a positive [lines]. */
    private fun sendWheel(change: PointerInputChange, event: PointerEvent, lines: Int) {
        val button = if (lines > 0) MouseButton.WHEEL_DOWN else MouseButton.WHEEL_UP
        val (row, col) = findCell(change.position)
        repeat((abs(lines) / SCROLL_LINES).coerceAtLeast(1)) {
            session.sendMouse(MouseReport(MouseAction.PRESS, button, row, col, readModifiers(event)))
        }
    }

    private fun sendArrows(lines: Int) {
        val key = if (lines > 0) TerminalKey.DOWN else TerminalKey.UP
        repeat(abs(lines)) { session.sendKey(key, KeyModifiers.NONE) }
    }

    /** The screen cell under [position], inside the screen. */
    private fun findCell(position: Offset): Pair<Int, Int> =
        findRow(position).coerceIn(0, session.rows - 1) to floor(position.x / metrics.width).toInt().coerceIn(0, session.cols - 1)

    private fun findRow(position: Offset): Int = floor(position.y / metrics.height).toInt()

    private fun readModifiers(event: PointerEvent): KeyModifiers = event.keyboardModifiers.let {
        KeyModifiers(shift = it.isShiftPressed, alt = it.isAltPressed, ctrl = it.isCtrlPressed)
    }

    /** Reports to the program: presses, releases and, when it asked, moves to another cell. */
    private inner class ProgramMouse : MouseTarget {
        private var lastCell: Pair<Int, Int>? = null

        override fun onPress(button: MouseButton, change: PointerInputChange, event: PointerEvent) =
            send(MouseAction.PRESS, button, change, event)

        override fun onRelease(button: MouseButton, change: PointerInputChange, event: PointerEvent) =
            send(MouseAction.RELEASE, button, change, event)

        override fun onMove(change: PointerInputChange, event: PointerEvent) {
            val cell = findCell(change.position)
            if (cell == lastCell) return
            val button = held.firstOrNull()
            send(if (button == null) MouseAction.MOVE else MouseAction.DRAG, button, change, event)
        }

        private fun send(action: MouseAction, button: MouseButton?, change: PointerInputChange, event: PointerEvent) {
            val cell = findCell(change.position)
            lastCell = cell
            session.sendMouse(MouseReport(action, button, cell.first, cell.second, readModifiers(event)))
        }
    }

    /** The terminal's own use: select with the left button, the menu on the right one, paste with the middle one. */
    private inner class SelectionMouse : MouseTarget {
        private val presses: Map<MouseButton, (PointerInputChange) -> Unit> = mapOf(
            MouseButton.LEFT to ::startSelection,
            MouseButton.RIGHT to { change -> state.menuPosition = change.position },
            MouseButton.MIDDLE to { _ -> state.pastePrimary() },
        )

        override fun onPress(button: MouseButton, change: PointerInputChange, event: PointerEvent) {
            presses[button]?.invoke(change)
        }

        override fun onRelease(button: MouseButton, change: PointerInputChange, event: PointerEvent) {
            if (button == MouseButton.LEFT) state.finishSelection()
        }

        override fun onMove(change: PointerInputChange, event: PointerEvent) {
            if (MouseButton.LEFT !in held) return
            val current = state.selection ?: return
            scrollPastEdge(change.position)
            state.selection = current.moveFocus(findPoint(change.position, current.unit))
        }

        private fun startSelection(change: PointerInputChange) {
            val count = clicks.count(change.uptimeMillis, findCell(change.position))
            val unit = CLICK_UNITS[count - 1]
            val point = findPoint(change.position, unit)
            state.selection = TextSelection(point, point, unit, session.isAlternateScreen)
        }

        /** Dragging above or below the terminal scrolls the history one line per move. */
        private fun scrollPastEdge(position: Offset) {
            val row = findRow(position)
            if (row < 0) state.scrollBy(1)
            if (row >= session.rows) state.scrollBy(-1)
        }

        private fun findPoint(position: Offset, unit: SelectionUnit): TextPoint {
            val row = findRow(position).coerceIn(0, session.rows - 1)
            val col = UNIT_COLUMNS.getValue(unit)(position.x / metrics.width, session.cols)
            return TextPoint(session.findLineNumber(row, state.scrollOffset), col)
        }
    }
}

/** Counts presses on the same cell within the double click time: 1, 2, 3, then 1 again. */
private class ClickCounter(private val timeoutMs: Long) {
    private var count = 0
    private var lastTime = 0L
    private var lastCell: Pair<Int, Int>? = null

    fun count(time: Long, cell: Pair<Int, Int>): Int {
        val isRepeat = time - lastTime <= timeoutMs && cell == lastCell
        count = if (isRepeat) count % CLICK_UNITS.size + 1 else 1
        lastTime = time
        lastCell = cell
        return count
    }
}
