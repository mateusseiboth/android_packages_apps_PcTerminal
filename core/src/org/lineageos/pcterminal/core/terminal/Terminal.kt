package org.lineageos.pcterminal.core.terminal

/** What a terminal tells its owner. Every method has an empty default. */
interface TerminalListener {
    /** Bytes the terminal answers to the program (status and cursor reports): write them to the pty. */
    fun onOutput(bytes: ByteArray) {}

    /** The program set the window title (OSC 0 or 2). */
    fun onTitle(title: String) {}

    fun onBell() {}
}

/** Mouse reporting the program asked for (DECSET 1000, 1002, 1003); recorded, not sent by this class. */
enum class MouseTracking { NONE, CLICKS, DRAG, ALL }

/**
 * An xterm-256color terminal: feed it the program's output with [write], draw a [snapshot], send
 * keys encoded by [KeyEncoder]. Not thread safe: call it from one thread or under one lock.
 */
class Terminal(
    rows: Int,
    cols: Int,
    private val scrollbackLimit: Int = DEFAULT_SCROLLBACK,
    private val listener: TerminalListener = object : TerminalListener {},
) : VtHandler {
    var rows = rows
        private set
    var cols = cols
        private set
    var title = ""
        private set
    var isApplicationCursorKeys = false
        private set
    var isApplicationKeypad = false
        private set
    var isBracketedPaste = false
        private set
    var mouseTracking = MouseTracking.NONE
        private set
    var isSgrMouse = false
        private set
    var isFocusReporting = false
        private set

    private val decoder = Utf8Decoder()
    private val parser = VtParser(this)
    private var main = Screen(rows, cols)
    private var alternate = Screen(rows, cols)
    private var screen = main
    private val scrollback = ArrayDeque<TerminalLine>()
    private val saved = HashMap<Screen, SavedCursor>()

    private var row = 0
    private var col = 0
    private var style = CellStyle.DEFAULT
    private var isPendingWrap = false
    private var isAutowrap = true
    private var isCursorVisible = true
    private var isOriginMode = false
    private var isInsertMode = false
    private var top = 0
    private var bottom = rows - 1
    private var tabStops = buildTabStops(cols)
    private var charsets = CharsetState()
    private var lastPrinted = 0

    private data class CharsetState(val g0: Charset = Charset.ASCII, val g1: Charset = Charset.ASCII, val isShifted: Boolean = false) {
        val active get() = if (isShifted) g1 else g0
    }

    private data class SavedCursor(
        val row: Int,
        val col: Int,
        val style: CellStyle,
        val isOriginMode: Boolean,
        val isPendingWrap: Boolean,
        val charsets: CharsetState,
    )

    /** Lines that can be read with [readLine]: the scrollback and then the screen. */
    val lineCount: Int get() = visibleScrollback + rows

    private val visibleScrollback: Int get() = if (screen === main) scrollback.size else 0
    private val line: TerminalLine get() = screen.lines[row]

    // ---- input ---------------------------------------------------------------------------------

    fun write(bytes: ByteArray, offset: Int = 0, length: Int = bytes.size - offset) {
        decoder.decode(bytes, offset, length, parser::feed)
    }

    // ---- reading -------------------------------------------------------------------------------

    /** Line [index] of the scrollback followed by the screen, 0 being the oldest. */
    fun readLine(index: Int): LineSnapshot {
        val scrolled = visibleScrollback
        val source = if (index < scrolled) scrollback[index] else screen.lines[index - scrolled]
        return LineSnapshots.build(source, cols)
    }

    /** What to draw, [scrollOffset] lines up from the bottom (clamped to the scrollback). */
    fun snapshot(scrollOffset: Int = 0): TerminalSnapshot {
        val offset = scrollOffset.coerceIn(0, visibleScrollback)
        val first = visibleScrollback - offset
        val cursorRow = row + offset
        return TerminalSnapshot(
            rows = rows,
            cols = cols,
            lines = (first until first + rows).map(::readLine),
            cursorRow = cursorRow,
            cursorCol = col,
            isCursorVisible = isCursorVisible && cursorRow < rows,
            title = title,
            isAlternateScreen = screen === alternate,
            scrollbackSize = visibleScrollback,
            scrollOffset = offset,
        )
    }

    // ---- resize --------------------------------------------------------------------------------

    fun resize(newRows: Int, newCols: Int) {
        require(newRows > 0 && newCols > 0)
        if (newRows == rows && newCols == cols) return
        val active = screen
        val inactive = if (active === main) alternate else main
        row = resizeScreen(active, row, newRows, newCols)
        saved[inactive]?.let { saved[inactive] = it.copy(row = resizeScreen(inactive, it.row, newRows, newCols)) }
            ?: resizeScreen(inactive, 0, newRows, newCols)
        rows = newRows
        cols = newCols
        tabStops = buildTabStops(newCols)
        top = 0
        bottom = newRows - 1
        col = col.coerceAtMost(newCols - 1)
        isPendingWrap = false
        saved.replaceAll { _, it -> it.copy(row = it.row.coerceAtMost(newRows - 1), col = it.col.coerceAtMost(newCols - 1)) }
    }

    private fun resizeScreen(target: Screen, cursorRow: Int, newRows: Int, newCols: Int): Int {
        val lines = target.lines
        val withScrollback = target === main
        lines.forEach { it.resize(newCols) }
        var at = cursorRow
        while (lines.size > newRows && lines.size - 1 > at && lines.last().isBlank()) lines.removeAt(lines.size - 1)
        while (lines.size > newRows && at >= newRows) {
            lines.removeAt(0).let { if (withScrollback) pushScrollback(it) }
            at--
        }
        while (lines.size > newRows) lines.removeAt(lines.size - 1)
        while (lines.size < newRows) at += growScreen(lines, newCols, withScrollback)
        return at.coerceIn(0, newRows - 1)
    }

    /** Adds a line: one back from the scrollback on top (returns 1), or a blank one below (returns 0). */
    private fun growScreen(lines: MutableList<TerminalLine>, newCols: Int, withScrollback: Boolean): Int {
        if (withScrollback && scrollback.isNotEmpty()) {
            lines.add(0, scrollback.removeLast().also { it.resize(newCols) })
            return 1
        }
        lines.add(TerminalLine(newCols))
        return 0
    }

    // ---- VtHandler -----------------------------------------------------------------------------

    override fun onPrint(codePoint: Int) {
        printChar(charsets.active.map(codePoint))
    }

    override fun onExecute(control: Int) {
        CONTROLS[control]?.invoke(this)
    }

    override fun onCsi(params: CsiParams, intermediates: String, final: Char) {
        CSI_COMMANDS["${params.prefix ?: ""}$intermediates$final"]?.invoke(this, params)
    }

    override fun onEsc(intermediates: String, final: Char) {
        ESC_COMMANDS["$intermediates$final"]?.invoke(this)
    }

    override fun onOsc(text: String) {
        val code = text.substringBefore(';', "")
        if (code !in TITLE_CODES) return
        title = text.substringAfter(';')
        listener.onTitle(title)
    }

    // ---- printing ------------------------------------------------------------------------------

    private fun printChar(codePoint: Int) {
        val width = CharWidth.of(codePoint)
        if (width == 0) return
        if (isPendingWrap) wrapLine()
        if (width == 2 && col == cols - 1) fitWideAtEdge()
        if (width > cols) return
        if (isInsertMode) line.insertBlanks(col, width, style.bg)
        putChar(codePoint, width)
        lastPrinted = codePoint
        advance(width)
    }

    private fun fitWideAtEdge() {
        if (isAutowrap) {
            line.erase(col, cols, style.bg)
            wrapLine()
            return
        }
        col = (cols - 2).coerceAtLeast(0)
    }

    private fun wrapLine() {
        line.isWrapped = true
        col = 0
        isPendingWrap = false
        index()
    }

    private fun putChar(codePoint: Int, width: Int) {
        val target = line
        if (target.codes[col] == TerminalLine.WIDE_TAIL && col > 0) target.codes[col - 1] = TerminalLine.BLANK
        val end = col + width
        if (end < cols && target.codes[end] == TerminalLine.WIDE_TAIL) target.codes[end] = TerminalLine.BLANK
        target.set(col, codePoint, style)
        if (width == 2) target.set(col + 1, TerminalLine.WIDE_TAIL, style)
    }

    private fun advance(width: Int) {
        if (col + width < cols) {
            col += width
            return
        }
        col = cols - 1
        isPendingWrap = isAutowrap
    }

    private fun repeatLast(count: Int) {
        if (lastPrinted == 0) return
        repeat(count) { printChar(lastPrinted) }
    }

    // ---- controls ------------------------------------------------------------------------------

    private fun bell() = listener.onBell()

    private fun backspace() {
        isPendingWrap = false
        col = (col - 1).coerceAtLeast(0)
    }

    private fun carriageReturn() {
        isPendingWrap = false
        col = 0
    }

    private fun lineFeed() {
        isPendingWrap = false
        index()
    }

    private fun index() {
        if (row == bottom) {
            scrollRegionUp(1)
            return
        }
        if (row < rows - 1) row++
    }

    private fun reverseIndex() {
        isPendingWrap = false
        if (row == top) {
            screen.scrollDown(top, bottom, 1, style.bg)
            return
        }
        if (row > 0) row--
    }

    private fun nextLine() {
        carriageReturn()
        index()
    }

    private fun shiftCharset(isShifted: Boolean) {
        charsets = charsets.copy(isShifted = isShifted)
    }

    private fun designateG0(charset: Charset) {
        charsets = charsets.copy(g0 = charset)
    }

    private fun designateG1(charset: Charset) {
        charsets = charsets.copy(g1 = charset)
    }

    // ---- tabs ----------------------------------------------------------------------------------

    private fun buildTabStops(width: Int) = BooleanArray(width) { it > 0 && it % TAB_WIDTH == 0 }

    private fun forwardTab(count: Int = 1) {
        isPendingWrap = false
        repeat(count) { col = ((col + 1) until cols).firstOrNull { tabStops[it] } ?: (cols - 1) }
    }

    private fun backTab(count: Int) {
        isPendingWrap = false
        repeat(count) { col = (col - 1 downTo 0).firstOrNull { tabStops[it] } ?: 0 }
    }

    private fun setTabStop() {
        tabStops[col] = true
    }

    private fun clearTabs(mode: Int) {
        when (mode) {
            0 -> tabStops[col] = false
            3 -> tabStops.fill(false)
        }
    }

    // ---- cursor --------------------------------------------------------------------------------

    private fun moveTo(newRow: Int, newCol: Int) {
        isPendingWrap = false
        row = newRow.coerceIn(0, rows - 1)
        col = newCol.coerceIn(0, cols - 1)
    }

    /** CUP: one-based, relative to the scroll region in origin mode. */
    private fun moveToPosition(oneBasedRow: Int, oneBasedCol: Int) {
        val newRow = if (isOriginMode) (top + oneBasedRow - 1).coerceIn(top, bottom) else oneBasedRow - 1
        moveTo(newRow, oneBasedCol - 1)
    }

    private fun moveToRow(oneBasedRow: Int) = moveToPosition(oneBasedRow, col + 1)

    /** CUU stops at the top margin when the cursor is inside the scroll region. */
    private fun moveUp(count: Int) {
        val limit = if (row >= top) top else 0
        moveTo((row - count).coerceAtLeast(limit), col)
    }

    private fun moveDown(count: Int) {
        val limit = if (row <= bottom) bottom else rows - 1
        moveTo((row + count).coerceAtMost(limit), col)
    }

    private fun homeCursor() = moveToPosition(1, 1)

    private fun saveCursor() {
        saved[screen] = SavedCursor(row, col, style, isOriginMode, isPendingWrap, charsets)
    }

    private fun restoreCursor() {
        val cursor = saved[screen] ?: SavedCursor(0, 0, CellStyle.DEFAULT, false, false, CharsetState())
        moveTo(cursor.row, cursor.col)
        style = cursor.style
        isOriginMode = cursor.isOriginMode
        isPendingWrap = cursor.isPendingWrap
        charsets = cursor.charsets
    }

    // ---- reports -------------------------------------------------------------------------------

    private fun reply(text: String) = listener.onOutput(text.toByteArray(Charsets.UTF_8))

    private fun reportStatus(kind: Int) {
        when (kind) {
            5 -> reply("${CSI}0n")
            6 -> reply("$CSI${row - (if (isOriginMode) top else 0) + 1};${col + 1}R")
        }
    }

    private fun reportAttributes(params: CsiParams) {
        if (params.read(0, 0) == 0) reply("$CSI?62;22c")
    }

    // ---- erasing and editing -------------------------------------------------------------------

    private fun eraseInDisplay(mode: Int) {
        isPendingWrap = false
        when (mode) {
            0 -> eraseLines(row + 1, rows).also { line.erase(col, cols, style.bg) }
            1 -> eraseLines(0, row).also { line.erase(0, col + 1, style.bg) }
            2 -> eraseLines(0, rows)
            3 -> scrollback.clear()
        }
    }

    private fun eraseLines(from: Int, to: Int) {
        for (r in from until to) screen.lines[r].apply { erase(0, size, style.bg) }.isWrapped = false
    }

    private fun eraseInLine(mode: Int) {
        isPendingWrap = false
        when (mode) {
            0 -> line.erase(col, cols, style.bg)
            1 -> line.erase(0, col + 1, style.bg)
            2 -> line.erase(0, cols, style.bg)
        }
    }

    private fun eraseChars(count: Int) {
        isPendingWrap = false
        line.erase(col, col + count, style.bg)
    }

    private fun insertChars(count: Int) {
        isPendingWrap = false
        line.insertBlanks(col, count, style.bg)
    }

    private fun deleteChars(count: Int) {
        isPendingWrap = false
        line.deleteCells(col, count, style.bg)
    }

    private fun isInsideRegion() = row in top..bottom

    private fun insertLines(count: Int) {
        if (!isInsideRegion()) return
        screen.scrollDown(row, bottom, count, style.bg)
        carriageReturn()
    }

    private fun deleteLines(count: Int) {
        if (!isInsideRegion()) return
        screen.scrollUp(row, bottom, count, style.bg, null)
        carriageReturn()
    }

    private fun scrollRegionUp(count: Int) {
        val evict = if (screen === main && top == 0) ::pushScrollback else null
        screen.scrollUp(top, bottom, count, style.bg, evict)
    }

    private fun scrollRegionDown(count: Int) = screen.scrollDown(top, bottom, count, style.bg)

    private fun pushScrollback(evicted: TerminalLine) {
        if (scrollbackLimit <= 0) return
        scrollback.addLast(evicted)
        while (scrollback.size > scrollbackLimit) scrollback.removeFirst()
    }

    private fun setScrollRegion(params: CsiParams) {
        val first = params.read(0, 1)
        val last = params.read(1, rows).coerceAtMost(rows)
        if (first >= last) return
        top = first - 1
        bottom = last - 1
        homeCursor()
    }

    private fun fillAlignment() {
        screen.lines.forEach { l -> for (c in 0 until l.size) l.set(c, 'E'.code, CellStyle.DEFAULT) }
        moveTo(0, 0)
    }

    // ---- styles --------------------------------------------------------------------------------

    private fun applySgr(params: CsiParams) {
        style = Sgr.apply(style, params)
    }

    // ---- modes ---------------------------------------------------------------------------------

    private fun setModes(params: CsiParams, isOn: Boolean) {
        for (i in 0 until params.size) {
            if (params.read(i, 0) == INSERT_MODE) isInsertMode = isOn
        }
    }

    private fun setPrivateModes(params: CsiParams, isOn: Boolean) {
        for (i in 0 until params.size) PRIVATE_MODES[params.read(i, 0)]?.invoke(this, isOn)
    }

    private fun setOriginMode(isOn: Boolean) {
        isOriginMode = isOn
        homeCursor()
    }

    private fun setMouse(mode: MouseTracking, isOn: Boolean) {
        mouseTracking = if (isOn) mode else MouseTracking.NONE
    }

    private fun useAlternate(isOn: Boolean) {
        val target = if (isOn) alternate else main
        if (target === screen) return
        screen = target
        isPendingWrap = false
    }

    private fun clearAlternate() = alternate.lines.forEach { it.erase(0, it.size, TermColor.DEFAULT); it.isWrapped = false }

    private fun setAlternate1047(isOn: Boolean) {
        if (!isOn && screen === alternate) clearAlternate()
        useAlternate(isOn)
    }

    private fun setAlternate1049(isOn: Boolean) {
        if (isOn && screen === main) {
            saveCursor()
            useAlternate(true)
            clearAlternate()
            return
        }
        if (!isOn && screen === alternate) {
            useAlternate(false)
            restoreCursor()
        }
    }

    private fun saveOrRestoreCursor(isSave: Boolean) = if (isSave) saveCursor() else restoreCursor()

    // ---- resets --------------------------------------------------------------------------------

    private fun softReset() {
        isCursorVisible = true
        isInsertMode = false
        isOriginMode = false
        isAutowrap = true
        isApplicationCursorKeys = false
        isApplicationKeypad = false
        top = 0
        bottom = rows - 1
        style = CellStyle.DEFAULT
        charsets = CharsetState()
        saved.remove(screen)
        isPendingWrap = false
    }

    private fun fullReset() {
        softReset()
        main = Screen(rows, cols)
        alternate = Screen(rows, cols)
        screen = main
        saved.clear()
        tabStops = buildTabStops(cols)
        isBracketedPaste = false
        mouseTracking = MouseTracking.NONE
        isSgrMouse = false
        isFocusReporting = false
        title = ""
        lastPrinted = 0
        moveTo(0, 0)
    }

    companion object {
        const val DEFAULT_SCROLLBACK = 2000
        private const val TAB_WIDTH = 8
        private const val INSERT_MODE = 4
        private const val CSI = "\u001b["
        private val TITLE_CODES = setOf("0", "2")

        private fun control(action: Terminal.() -> Unit) = action
        private fun esc(action: Terminal.() -> Unit) = action
        private fun csi(action: Terminal.(CsiParams) -> Unit) = action
        private fun mode(action: Terminal.(Boolean) -> Unit) = action

        private val CONTROLS: Map<Int, Terminal.() -> Unit> = mapOf(
            0x07 to Terminal::bell,
            0x08 to Terminal::backspace,
            0x09 to control { forwardTab() },
            0x0A to Terminal::lineFeed,
            0x0B to Terminal::lineFeed,
            0x0C to Terminal::lineFeed,
            0x0D to Terminal::carriageReturn,
            0x0E to control { shiftCharset(true) },
            0x0F to control { shiftCharset(false) },
        )

        private val ESC_COMMANDS: Map<String, Terminal.() -> Unit> = mapOf(
            "7" to Terminal::saveCursor,
            "8" to Terminal::restoreCursor,
            "D" to Terminal::lineFeed,
            "E" to Terminal::nextLine,
            "M" to Terminal::reverseIndex,
            "H" to Terminal::setTabStop,
            "c" to Terminal::fullReset,
            "=" to esc { isApplicationKeypad = true },
            ">" to esc { isApplicationKeypad = false },
            "#8" to Terminal::fillAlignment,
        ) + Charset.DESIGNATORS.flatMap { (final, charset) ->
            listOf("($final" to esc { designateG0(charset) }, ")$final" to esc { designateG1(charset) })
        }

        private val CSI_COMMANDS: Map<String, Terminal.(CsiParams) -> Unit> = mapOf(
            "@" to csi { p -> insertChars(p.read(0, 1)) },
            "A" to csi { p -> moveUp(p.read(0, 1)) },
            "B" to csi { p -> moveDown(p.read(0, 1)) },
            "e" to csi { p -> moveDown(p.read(0, 1)) },
            "C" to csi { p -> moveTo(row, col + p.read(0, 1)) },
            "a" to csi { p -> moveTo(row, col + p.read(0, 1)) },
            "D" to csi { p -> moveTo(row, col - p.read(0, 1)) },
            "E" to csi { p -> moveDown(p.read(0, 1)).also { col = 0 } },
            "F" to csi { p -> moveUp(p.read(0, 1)).also { col = 0 } },
            "G" to csi { p -> moveTo(row, p.read(0, 1) - 1) },
            "`" to csi { p -> moveTo(row, p.read(0, 1) - 1) },
            "H" to csi { p -> moveToPosition(p.read(0, 1), p.read(1, 1)) },
            "f" to csi { p -> moveToPosition(p.read(0, 1), p.read(1, 1)) },
            "d" to csi { p -> moveToRow(p.read(0, 1)) },
            "I" to csi { p -> forwardTab(p.read(0, 1)) },
            "Z" to csi { p -> backTab(p.read(0, 1)) },
            "J" to csi { p -> eraseInDisplay(p.read(0, 0)) },
            "K" to csi { p -> eraseInLine(p.read(0, 0)) },
            "X" to csi { p -> eraseChars(p.read(0, 1)) },
            "P" to csi { p -> deleteChars(p.read(0, 1)) },
            "L" to csi { p -> insertLines(p.read(0, 1)) },
            "M" to csi { p -> deleteLines(p.read(0, 1)) },
            "S" to csi { p -> scrollRegionUp(p.read(0, 1)) },
            "T" to csi { p -> scrollRegionDown(p.read(0, 1)) },
            "b" to csi { p -> repeatLast(p.read(0, 1)) },
            "g" to csi { p -> clearTabs(p.read(0, 0)) },
            "m" to Terminal::applySgr,
            "r" to Terminal::setScrollRegion,
            "s" to csi { _ -> saveCursor() },
            "u" to csi { _ -> restoreCursor() },
            "n" to csi { p -> reportStatus(p.read(0, 0)) },
            "c" to Terminal::reportAttributes,
            ">c" to csi { p -> if (p.read(0, 0) == 0) reply("$CSI>0;10;1c") },
            "h" to csi { p -> setModes(p, true) },
            "l" to csi { p -> setModes(p, false) },
            "?h" to csi { p -> setPrivateModes(p, true) },
            "?l" to csi { p -> setPrivateModes(p, false) },
            "!p" to csi { _ -> softReset() },
        )

        private val PRIVATE_MODES: Map<Int, Terminal.(Boolean) -> Unit> = mapOf(
            1 to mode { on -> isApplicationCursorKeys = on },
            6 to Terminal::setOriginMode,
            7 to mode { on -> isAutowrap = on; if (!on) isPendingWrap = false },
            25 to mode { on -> isCursorVisible = on },
            47 to Terminal::useAlternate,
            66 to mode { on -> isApplicationKeypad = on },
            1000 to mode { on -> setMouse(MouseTracking.CLICKS, on) },
            1002 to mode { on -> setMouse(MouseTracking.DRAG, on) },
            1003 to mode { on -> setMouse(MouseTracking.ALL, on) },
            1004 to mode { on -> isFocusReporting = on },
            1006 to mode { on -> isSgrMouse = on },
            1047 to Terminal::setAlternate1047,
            1048 to Terminal::saveOrRestoreCursor,
            1049 to Terminal::setAlternate1049,
            2004 to mode { on -> isBracketedPaste = on },
        )
    }
}
