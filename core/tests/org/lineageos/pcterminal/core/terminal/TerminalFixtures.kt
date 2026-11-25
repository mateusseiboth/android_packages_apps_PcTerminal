package org.lineageos.pcterminal.core.terminal

/** Remembers what a terminal sent back to the program, its titles and bells. */
class RecordingListener : TerminalListener {
    val output = StringBuilder()
    val titles = mutableListOf<String>()
    var bells = 0

    override fun onOutput(bytes: ByteArray) {
        output.append(String(bytes, Charsets.UTF_8))
    }

    override fun onTitle(title: String) {
        titles += title
    }

    override fun onBell() {
        bells++
    }
}

fun buildTerminal(rows: Int = 5, cols: Int = 10, scrollback: Int = 100, listener: TerminalListener = RecordingListener()) =
    Terminal(rows, cols, scrollback, listener)

fun Terminal.feed(text: String) = write(text.toByteArray(Charsets.UTF_8))

/** The visible lines without trailing blanks. */
fun Terminal.readScreen(): List<String> = snapshot().lines.map { it.text.trimEnd() }

fun Terminal.readRow(row: Int): String = snapshot().lines[row].text.trimEnd()

fun Terminal.readCursor(): Pair<Int, Int> = snapshot().let { it.cursorRow to it.cursorCol }

const val ESC = "\u001b"
const val CSI = "\u001b["
