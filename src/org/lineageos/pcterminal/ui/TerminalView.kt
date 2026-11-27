package org.lineageos.pcterminal.ui

import android.graphics.Paint
import android.graphics.Typeface
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.focusable
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.material3.MaterialTheme
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.ui.Modifier
import androidx.compose.ui.focus.FocusRequester
import androidx.compose.ui.focus.focusRequester
import androidx.compose.ui.graphics.drawscope.drawIntoCanvas
import androidx.compose.ui.graphics.nativeCanvas
import androidx.compose.ui.graphics.toArgb
import androidx.compose.ui.input.key.KeyEventType
import androidx.compose.ui.input.key.onPreviewKeyEvent
import androidx.compose.ui.input.key.type
import androidx.compose.ui.input.pointer.PointerEventType
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.layout.onSizeChanged
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.platform.LocalViewConfiguration
import androidx.compose.ui.unit.sp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import org.lineageos.pcterminal.TerminalClipboard
import org.lineageos.pcterminal.TerminalTab
import org.lineageos.pcterminal.core.terminal.CellStyle
import org.lineageos.pcterminal.core.terminal.SelectedRange
import org.lineageos.pcterminal.core.terminal.StyledRun
import org.lineageos.pcterminal.core.terminal.TermColor
import org.lineageos.pcterminal.core.terminal.TerminalSnapshot

private val FONT_SIZE = 14.sp
private const val ITALIC_SKEW = -0.25f
private const val DIM_ALPHA = 0x99
private const val CURSOR_ALPHA = 0xB0
private const val SELECTION_ALPHA = 0x66

/** The size of one character cell and where its text sits. */
class CellMetrics(paint: Paint) {
    val width = paint.measureText("M")
    val height: Float
    val baseline: Float

    init {
        val font = paint.fontMetrics
        height = font.descent - font.ascent + font.leading
        baseline = -font.ascent
    }
}

private data class Palette(val foreground: Int, val background: Int, val selection: Int)

/** Shortcuts the view handles itself; the others go to the window. */
private val VIEW_SHORTCUTS: Map<Shortcut, (TerminalViewState) -> Unit> = mapOf(
    Shortcut.COPY to TerminalViewState::copy,
    Shortcut.PASTE to TerminalViewState::paste,
    Shortcut.SELECT_ALL to TerminalViewState::selectAll,
    Shortcut.SCROLL_UP to { it.scrollBy(it.session.rows / 2) },
    Shortcut.SCROLL_DOWN to { it.scrollBy(-it.session.rows / 2) },
)

/**
 * One terminal drawn with the platform's monospace font, with the mouse selection on top. Keys go to its
 * session, the mouse to [MouseHandler], the right button's menu is [TerminalMenu].
 */
@Composable
fun TerminalView(tab: TerminalTab, onSize: (Int, Int) -> Unit, onShortcut: (Shortcut) -> Unit, modifier: Modifier = Modifier) {
    val version by tab.version.collectAsStateWithLifecycle()
    val density = LocalDensity.current
    val paint = remember(density) {
        Paint(Paint.ANTI_ALIAS_FLAG).apply {
            typeface = Typeface.MONOSPACE
            textSize = with(density) { FONT_SIZE.toPx() }
        }
    }
    val metrics = remember(paint) { CellMetrics(paint) }
    val colors = MaterialTheme.colorScheme
    val palette = Palette(colors.onSurface.toArgb(), colors.surface.toArgb(), (colors.primary.toArgb() and 0x00FFFFFF) or (SELECTION_ALPHA shl 24))
    val focus = remember { FocusRequester() }
    val context = LocalContext.current
    val clipboard = remember(context) { TerminalClipboard(context) }
    val state = remember(tab) { TerminalViewState(tab.session, clipboard) }
    val doubleClickMs = LocalViewConfiguration.current.doubleTapTimeoutMillis
    val mouse = remember(state, metrics, doubleClickMs) { MouseHandler(state, metrics, doubleClickMs) }
    val currentOnShortcut by rememberUpdatedState(onShortcut)
    val keys = remember(state) {
        KeyHandler { shortcut -> VIEW_SHORTCUTS[shortcut]?.invoke(state) ?: currentOnShortcut(shortcut) }
    }
    LaunchedEffect(tab) { focus.requestFocus() }
    Box(modifier.fillMaxSize()) {
        TerminalCanvas(tab, state, version, paint, metrics, palette, focus, keys, mouse, onSize)
        TerminalMenu(state, onClose = { focus.requestFocus() })
    }
}

@Composable
private fun TerminalCanvas(
    tab: TerminalTab,
    state: TerminalViewState,
    version: Long,
    paint: Paint,
    metrics: CellMetrics,
    palette: Palette,
    focus: FocusRequester,
    keys: KeyHandler,
    mouse: MouseHandler,
    onSize: (Int, Int) -> Unit,
) {
    Canvas(
        Modifier
            .fillMaxSize()
            .onSizeChanged { size ->
                val rows = (size.height / metrics.height).toInt().coerceAtLeast(1)
                val cols = (size.width / metrics.width).toInt().coerceAtLeast(1)
                tab.session.resize(rows, cols)
                onSize(rows, cols)
            }
            .focusRequester(focus)
            .focusable()
            .onPreviewKeyEvent { event ->
                if (event.type != KeyEventType.KeyDown) return@onPreviewKeyEvent false
                val used = keys.onKeyDown(event.nativeKeyEvent, tab.session)
                if (used && !event.nativeKeyEvent.isShiftPressed) state.scrollToBottom()
                used
            }
            .pointerInput(mouse) {
                awaitPointerEventScope {
                    while (true) {
                        val event = awaitPointerEvent()
                        if (event.type == PointerEventType.Press) focus.requestFocus()
                        mouse.onPointerEvent(event)
                    }
                }
            },
    ) {
        version.let { } // redraw on every change of the terminal
        val snapshot = tab.session.snapshot(state.scrollOffset)
        val selected = state.selection?.let(tab.session::resolveSelection)
        drawIntoCanvas { drawSnapshot(it.nativeCanvas, snapshot, selected, paint, metrics, palette) }
    }
}

private fun drawSnapshot(
    canvas: android.graphics.Canvas,
    snapshot: TerminalSnapshot,
    selected: SelectedRange?,
    paint: Paint,
    metrics: CellMetrics,
    palette: Palette,
) {
    canvas.drawColor(palette.background)
    snapshot.lines.forEachIndexed { row, line ->
        line.runs.forEach { drawRun(canvas, it, row * metrics.height, paint, metrics, palette) }
    }
    selected?.let { drawSelection(canvas, snapshot, it, paint, metrics, palette) }
    if (snapshot.isCursorVisible) drawCursor(canvas, snapshot, paint, metrics, palette)
}

/** A translucent band over the selected cells of each row. */
private fun drawSelection(canvas: android.graphics.Canvas, snapshot: TerminalSnapshot, selected: SelectedRange, paint: Paint, metrics: CellMetrics, palette: Palette) {
    paint.color = palette.selection
    (0 until snapshot.rows).forEach { row ->
        val columns = selected.findColumns(snapshot.topLineNumber + row, snapshot.cols) ?: return@forEach
        val top = row * metrics.height
        canvas.drawRect(columns.first * metrics.width, top, (columns.last + 1) * metrics.width, top + metrics.height, paint)
    }
}

/** fg and bg of a style, with inverse, dim and invisible applied. */
private fun resolveColors(style: CellStyle, palette: Palette): Pair<Int, Int> {
    val fg = TermColor.toArgb(style.fg, palette.foreground)
    val bg = TermColor.toArgb(style.bg, palette.background)
    val (front, back) = if (style.isInverse) bg to fg else fg to bg
    val shown = if (style.isDim) (front and 0x00FFFFFF) or (DIM_ALPHA shl 24) else front
    return (if (style.isInvisible) back else shown) to back
}

private fun drawRun(canvas: android.graphics.Canvas, run: StyledRun, top: Float, paint: Paint, metrics: CellMetrics, palette: Palette) {
    val (fg, bg) = resolveColors(run.style, palette)
    val left = run.column * metrics.width
    if (bg != palette.background) {
        paint.color = bg
        canvas.drawRect(left, top, left + run.cells * metrics.width, top + metrics.height, paint)
    }
    if (run.text.isBlank()) return
    paint.color = fg
    paint.isFakeBoldText = run.style.isBold
    paint.textSkewX = if (run.style.isItalic) ITALIC_SKEW else 0f
    paint.isUnderlineText = run.style.isUnderline
    paint.isStrikeThruText = run.style.isStrikethrough
    canvas.drawText(run.text, left, top + metrics.baseline, paint)
}

private fun drawCursor(canvas: android.graphics.Canvas, snapshot: TerminalSnapshot, paint: Paint, metrics: CellMetrics, palette: Palette) {
    val left = snapshot.cursorCol * metrics.width
    val top = snapshot.cursorRow * metrics.height
    paint.color = (palette.foreground and 0x00FFFFFF) or (CURSOR_ALPHA shl 24)
    canvas.drawRect(left, top, left + metrics.width, top + metrics.height, paint)
}
