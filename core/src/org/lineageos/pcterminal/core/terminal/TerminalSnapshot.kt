package org.lineageos.pcterminal.core.terminal

/**
 * Cells of one style drawn from [column] on. A wide character is always a run of its own with
 * [cells] = 2; any other run has one cell per character of [text].
 */
data class StyledRun(val column: Int, val cells: Int, val text: String, val style: CellStyle)

/** One line as styled runs covering every column (blank cells are spaces). */
data class LineSnapshot(val runs: List<StyledRun>, val isWrapped: Boolean) {
    val text: String get() = runs.joinToString("") { it.text }
}

/**
 * An immutable copy of what the screen shows, for drawing. [lines] has [rows] entries; with a
 * [scrollOffset] the first lines come from the scrollback. The cursor is drawn at
 * ([cursorRow], [cursorCol]) when [isCursorVisible]. [topLineNumber] is the number of the first line
 * (see [Terminal.firstLineNumber]).
 */
data class TerminalSnapshot(
    val rows: Int,
    val cols: Int,
    val lines: List<LineSnapshot>,
    val cursorRow: Int,
    val cursorCol: Int,
    val isCursorVisible: Boolean,
    val title: String,
    val isAlternateScreen: Boolean,
    val scrollbackSize: Int,
    val scrollOffset: Int,
    val topLineNumber: Long,
)

internal object LineSnapshots {
    private const val SPACE = ' '.code

    fun build(line: TerminalLine, cols: Int): LineSnapshot {
        val runs = mutableListOf<StyledRun>()
        val text = StringBuilder()
        var start = 0
        var style: CellStyle? = null
        var col = 0
        fun flush() {
            style?.takeIf { text.isNotEmpty() }?.let { runs += StyledRun(start, col - start, text.toString(), it) }
            text.clear()
            start = col
        }
        while (col < cols) {
            val cellStyle = readStyle(line, col)
            val code = line.codes.getOrElse(col) { TerminalLine.BLANK }
            if (isWideHead(line, col, cols)) {
                flush()
                runs += StyledRun(col, 2, String(Character.toChars(code)), cellStyle)
                col += 2
                start = col
                style = null
                continue
            }
            if (cellStyle != style) flush()
            style = cellStyle
            text.appendCodePoint(if (code > 0) code else SPACE)
            col++
        }
        flush()
        return LineSnapshot(runs, line.isWrapped)
    }

    private fun readStyle(line: TerminalLine, col: Int) =
        if (col < line.size) line.readStyle(col) else CellStyle.DEFAULT

    private fun isWideHead(line: TerminalLine, col: Int, cols: Int) =
        col + 1 < cols && col + 1 < line.size && line.codes[col] > 0 && line.codes[col + 1] == TerminalLine.WIDE_TAIL
}
