package org.lineageos.pcterminal.ui

import android.graphics.Paint
import android.graphics.Typeface
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.focusable
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.material3.MaterialTheme
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
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
import androidx.compose.ui.platform.LocalClipboard
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.unit.sp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import kotlinx.coroutines.launch
import org.lineageos.pcterminal.TerminalTab
import org.lineageos.pcterminal.core.terminal.CellStyle
import org.lineageos.pcterminal.core.terminal.StyledRun
import org.lineageos.pcterminal.core.terminal.TermColor
import org.lineageos.pcterminal.core.terminal.TerminalSnapshot

private val FONT_SIZE = 14.sp
private const val SCROLL_LINES = 3
private const val ITALIC_SKEW = -0.25f
private const val DIM_ALPHA = 0x99
private const val CURSOR_ALPHA = 0xB0

/** The size of one character cell and where its text sits. */
private class CellMetrics(paint: Paint) {
    val width = paint.measureText("M")
    val height: Float
    val baseline: Float

    init {
        val font = paint.fontMetrics
        height = font.descent - font.ascent + font.leading
        baseline = -font.ascent
    }
}

private data class Palette(val foreground: Int, val background: Int)

/** One terminal drawn with the platform's monospace font; keys and the mouse wheel go to its session. */
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
    val palette = Palette(MaterialTheme.colorScheme.onSurface.toArgb(), MaterialTheme.colorScheme.surface.toArgb())
    var scrollOffset by remember(tab) { mutableIntStateOf(0) }
    val focus = remember { FocusRequester() }
    val clipboard = LocalClipboard.current
    val scope = rememberCoroutineScope()
    val keys = remember(tab) {
        KeyHandler { shortcut ->
            when (shortcut) {
                Shortcut.PASTE -> scope.launch {
                    clipboard.getClipEntry()?.clipData?.takeIf { it.itemCount > 0 }?.getItemAt(0)?.text?.let { tab.session.paste(it.toString()) }
                }
                Shortcut.SCROLL_UP -> scrollOffset += tab.session.snapshot().rows / 2
                Shortcut.SCROLL_DOWN -> scrollOffset = (scrollOffset - tab.session.snapshot().rows / 2).coerceAtLeast(0)
                else -> onShortcut(shortcut)
            }
        }
    }
    LaunchedEffect(tab) { focus.requestFocus() }
    Canvas(
        modifier
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
                if (used && !event.nativeKeyEvent.isShiftPressed) scrollOffset = 0
                used
            }
            .pointerInput(tab) {
                awaitPointerEventScope {
                    while (true) {
                        val event = awaitPointerEvent()
                        if (event.type == PointerEventType.Press) focus.requestFocus()
                        if (event.type != PointerEventType.Scroll) continue
                        val lines = (event.changes.first().scrollDelta.y * SCROLL_LINES).toInt()
                        scrollOffset = (scrollOffset - lines).coerceAtLeast(0)
                    }
                }
            },
    ) {
        version.let { } // redraw on every change of the terminal
        val snapshot = tab.session.snapshot(scrollOffset)
        drawIntoCanvas { drawSnapshot(it.nativeCanvas, snapshot, paint, metrics, palette) }
    }
}

private fun drawSnapshot(canvas: android.graphics.Canvas, snapshot: TerminalSnapshot, paint: Paint, metrics: CellMetrics, palette: Palette) {
    canvas.drawColor(palette.background)
    snapshot.lines.forEachIndexed { row, line ->
        line.runs.forEach { drawRun(canvas, it, row * metrics.height, paint, metrics, palette) }
    }
    if (snapshot.isCursorVisible) drawCursor(canvas, snapshot, paint, metrics, palette)
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
