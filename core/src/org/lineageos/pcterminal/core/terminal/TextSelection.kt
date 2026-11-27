package org.lineageos.pcterminal.core.terminal

/**
 * A place on the terminal: a [line] number (see [Terminal.firstLineNumber]), which stays with its text
 * while output scrolls, and a [col]umn.
 */
data class TextPoint(val line: Long, val col: Int) : Comparable<TextPoint> {
    override fun compareTo(other: TextPoint): Int = compareValuesBy(this, other, TextPoint::line, TextPoint::col)
}

/** What a selection grows by: characters (drag), words (double click) or lines (triple click). */
enum class SelectionUnit { CHARACTER, WORD, LINE }

/**
 * What the mouse selected, from where it was pressed ([anchor]) to where it is now ([focus]). The points
 * of a CHARACTER selection are edges between cells (0 to cols); those of WORD and LINE are cells. A
 * selection belongs to the screen it was made on ([isAlternateScreen]) and is not shown on the other.
 */
data class TextSelection(val anchor: TextPoint, val focus: TextPoint, val unit: SelectionUnit, val isAlternateScreen: Boolean) {
    fun moveFocus(point: TextPoint): TextSelection = copy(focus = point)
}

/** The selected cells: from [start] up to [end] (excluded), every cell of the lines between. */
data class SelectedRange(val start: TextPoint, val end: TextPoint) {
    val isEmpty: Boolean get() = start >= end

    /** The selected columns of [line], or null. */
    fun findColumns(line: Long, cols: Int): IntRange? {
        if (line < start.line || line > end.line) return null
        val from = if (line == start.line) start.col else 0
        val to = if (line == end.line) end.col else cols
        return (from until to).takeUnless { it.isEmpty() }
    }
}

/** Turns selections into the cells they cover and the text they copy, on a terminal's lines. */
object TextSelector {
    /** Besides letters and digits, what a double click takes as part of a word (paths, URLs, user@host). */
    private val WORD_SYMBOLS = ":@-./_~?&=%+#".map { it.code }.toSet()
    private const val SPACE = ' '.code
    private const val TAIL = -1
    private const val WORD_CLASS = -2
    private const val SPACE_CLASS = -3

    private val RESOLVERS: Map<SelectionUnit, (NumberedLines, TextPoint, TextPoint) -> SelectedRange> = mapOf(
        SelectionUnit.CHARACTER to { _, low, high -> SelectedRange(low, high) },
        SelectionUnit.WORD to { lines, low, high -> SelectedRange(lines.findWordStart(low), lines.findWordEnd(high)) },
        SelectionUnit.LINE to { lines, low, high ->
            SelectedRange(TextPoint(lines.findLogicalStart(low.line), 0), TextPoint(lines.findLogicalEnd(high.line), lines.cols))
        },
    )

    /** The cells [selection] covers now, or null when it covers none or belongs to the other screen. */
    fun resolve(terminal: Terminal, selection: TextSelection): SelectedRange? {
        if (selection.isAlternateScreen != terminal.isAlternateScreen) return null
        val lines = NumberedLines(terminal)
        val (low, high) = listOf(selection.anchor, selection.focus).sorted()
        val range = RESOLVERS.getValue(selection.unit)(lines, low, high)
        return lines.clamp(range).takeUnless { it.isEmpty }
    }

    /** The text of [range]: wrapped lines joined, other lines ended by a newline and without trailing blanks. */
    fun readText(terminal: Terminal, range: SelectedRange): String {
        val lines = NumberedLines(terminal)
        val text = StringBuilder()
        for (line in range.start.line..range.end.line) {
            val cells = lines.readCells(line) ?: continue
            val segment = readSegment(cells, range.findColumns(line, lines.cols) ?: IntRange.EMPTY)
            val isWrapped = lines.isWrapped(line)
            text.append(if (isWrapped) segment else segment.trimEnd(' '))
            if (!isWrapped && line != range.end.line) text.append('\n')
        }
        return text.toString()
    }

    /** Everything from the oldest line of the scrollback to the last line with text, or null when all is blank. */
    fun selectAll(terminal: Terminal): TextSelection? {
        val last = (terminal.lineCount - 1 downTo 0).firstOrNull { terminal.readLine(it).text.isNotBlank() } ?: return null
        val first = terminal.firstLineNumber
        return TextSelection(TextPoint(first, 0), TextPoint(first + last, terminal.cols), SelectionUnit.CHARACTER, terminal.isAlternateScreen)
    }

    private fun readSegment(cells: IntArray, columns: IntRange): String {
        if (columns.isEmpty()) return ""
        val from = if (cells[columns.first] == TAIL) columns.first - 1 else columns.first
        val text = StringBuilder()
        (from..columns.last).map { cells[it] }.filter { it != TAIL }.forEach { text.appendCodePoint(it) }
        return text.toString()
    }

    /** One code point per cell, [TAIL] for the second cell of a wide character. */
    private fun toCells(line: LineSnapshot, cols: Int): IntArray {
        val cells = IntArray(cols) { SPACE }
        line.runs.forEach { run ->
            val points = run.text.codePoints().toArray()
            val isWide = points.size != run.cells
            if (isWide) {
                cells[run.column] = points[0]
                cells[run.column + 1] = TAIL
                return@forEach
            }
            points.copyInto(cells, run.column)
        }
        return cells
    }

    /** Word characters, blanks, and each other character by itself. */
    private fun classify(codePoint: Int): Int = when {
        Character.isLetterOrDigit(codePoint) || codePoint in WORD_SYMBOLS -> WORD_CLASS
        codePoint == SPACE -> SPACE_CLASS
        else -> codePoint
    }

    /** The terminal's lines by number, read once each. */
    private class NumberedLines(private val terminal: Terminal) {
        val cols = terminal.cols
        private val first = terminal.firstLineNumber
        private val last = first + terminal.lineCount - 1
        private val snapshots = HashMap<Long, LineSnapshot?>()
        private val cells = HashMap<Long, IntArray?>()

        private fun readLine(line: Long): LineSnapshot? = snapshots.getOrPut(line) { terminal.readNumberedLine(line) }

        fun readCells(line: Long): IntArray? = cells.getOrPut(line) { readLine(line)?.let { toCells(it, cols) } }

        fun isWrapped(line: Long): Boolean = readLine(line)?.isWrapped == true

        fun clamp(range: SelectedRange): SelectedRange =
            SelectedRange(maxOf(range.start, TextPoint(first, 0)), minOf(range.end, TextPoint(last, cols)))

        private fun classifyAt(point: TextPoint): Int? {
            val row = readCells(point.line) ?: return null
            val col = point.col.coerceIn(0, cols - 1)
            val code = if (row[col] == TAIL && col > 0) row[col - 1] else row[col]
            return classify(code)
        }

        private fun previous(point: TextPoint): TextPoint? = when {
            point.col > 0 -> TextPoint(point.line, point.col - 1)
            isWrapped(point.line - 1) -> TextPoint(point.line - 1, cols - 1)
            else -> null
        }

        private fun next(point: TextPoint): TextPoint? = when {
            point.col + 1 < cols -> TextPoint(point.line, point.col + 1)
            isWrapped(point.line) && readLine(point.line + 1) != null -> TextPoint(point.line + 1, 0)
            else -> null
        }

        /** The cells of the same class as [point]'s around it; another character stands alone. */
        private fun isSameWord(kind: Int?, point: TextPoint) = kind != null && kind < 0 && classifyAt(point) == kind

        fun findWordStart(point: TextPoint): TextPoint {
            val cell = point.copy(col = point.col.coerceIn(0, cols - 1))
            val kind = classifyAt(cell)
            return generateSequence(cell) { at -> previous(at)?.takeIf { isSameWord(kind, it) } }.last()
        }

        /** Exclusive: the edge after the word's last cell. */
        fun findWordEnd(point: TextPoint): TextPoint {
            val cell = point.copy(col = point.col.coerceIn(0, cols - 1))
            val kind = classifyAt(cell)
            val last = generateSequence(cell) { at -> next(at)?.takeIf { isSameWord(kind, it) } }.last()
            return TextPoint(last.line, last.col + 1)
        }

        /** The first screen line of the text line [line] belongs to (autowrap splits one into several). */
        fun findLogicalStart(line: Long): Long = generateSequence(line) { (it - 1).takeIf(::isWrapped) }.last()

        fun findLogicalEnd(line: Long): Long = generateSequence(line) { at -> (at + 1).takeIf { isWrapped(at) && readLine(it) != null } }.last()
    }
}
