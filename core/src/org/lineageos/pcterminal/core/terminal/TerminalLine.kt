package org.lineageos.pcterminal.core.terminal

/**
 * One row of cells, stored as parallel arrays. A code of [BLANK] draws as a space; [WIDE_TAIL]
 * is the second cell of a wide character.
 */
internal class TerminalLine(cols: Int) {
    var codes = IntArray(cols)
        private set
    var fg = IntArray(cols) { TermColor.DEFAULT }
        private set
    var bg = IntArray(cols) { TermColor.DEFAULT }
        private set
    var attrs = IntArray(cols)
        private set

    /** The text continues on the next line (autowrap). */
    var isWrapped = false

    val size: Int get() = codes.size

    fun set(col: Int, code: Int, style: CellStyle) {
        codes[col] = code
        fg[col] = style.fg
        bg[col] = style.bg
        attrs[col] = style.attributes
    }

    fun readStyle(col: Int) = CellStyle(fg[col], bg[col], attrs[col])

    /** Blanks [from, to) with [background] (the current background, as xterm does). */
    fun erase(from: Int, to: Int, background: Int) {
        for (col in from.coerceAtLeast(0) until to.coerceAtMost(size)) {
            codes[col] = BLANK
            fg[col] = TermColor.DEFAULT
            bg[col] = background
            attrs[col] = 0
        }
    }

    fun insertBlanks(col: Int, count: Int, background: Int) {
        val n = count.coerceAtMost(size - col)
        move(col, col + n, size - col - n)
        erase(col, col + n, background)
    }

    fun deleteCells(col: Int, count: Int, background: Int) {
        val n = count.coerceAtMost(size - col)
        move(col + n, col, size - col - n)
        erase(size - n, size, background)
    }

    private fun move(from: Int, to: Int, length: Int) {
        if (length <= 0) return
        listOf(codes, fg, bg, attrs).forEach { System.arraycopy(it, from, it, to, length) }
    }

    fun isBlank(): Boolean = codes.all { it == BLANK }

    fun resize(cols: Int) {
        if (cols == size) return
        codes = resized(codes, cols, BLANK)
        fg = resized(fg, cols, TermColor.DEFAULT)
        bg = resized(bg, cols, TermColor.DEFAULT)
        attrs = resized(attrs, cols, 0)
    }

    private fun resized(array: IntArray, cols: Int, fill: Int) = IntArray(cols) { array.getOrElse(it) { fill } }

    companion object {
        const val BLANK = 0
        const val WIDE_TAIL = -1
    }
}

/** The rows of one screen (main or alternate). */
internal class Screen(rows: Int, val cols: Int) {
    val lines: MutableList<TerminalLine> = MutableList(rows) { TerminalLine(cols) }

    /** Moves [top, bottom] up; lines leaving the top go to [evict] when given. */
    fun scrollUp(top: Int, bottom: Int, count: Int, background: Int, evict: ((TerminalLine) -> Unit)?) {
        repeat(count.coerceAtMost(bottom - top + 1)) {
            val line = lines.removeAt(top)
            evict?.invoke(line)
            lines.add(bottom, buildBlank(background))
        }
    }

    fun scrollDown(top: Int, bottom: Int, count: Int, background: Int) {
        repeat(count.coerceAtMost(bottom - top + 1)) {
            lines.removeAt(bottom)
            lines.add(top, buildBlank(background))
        }
    }

    fun buildBlank(background: Int) = TerminalLine(lines.firstOrNull()?.size ?: cols).also {
        it.erase(0, it.size, background)
    }
}
