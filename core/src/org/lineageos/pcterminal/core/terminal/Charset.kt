package org.lineageos.pcterminal.core.terminal

/** The character sets a program can designate as G0 or G1 (ESC ( x, ESC ) x). */
enum class Charset(private val table: Map<Int, Int>) {
    ASCII(emptyMap()),

    /** DEC Special Graphics: the line drawing characters of curses programs. */
    LINE_DRAWING(
        "`abcdefghijklmnopqrstuvwxyz{|}~".zip(
            "◆▒␉␌␍␊°±␤␋┘┐┌└┼⎺⎻─⎼⎽├┤┴┬│≤≥π≠£·",
        ).associate { (from, to) -> from.code to to.code },
    );

    fun map(codePoint: Int): Int = table[codePoint] ?: codePoint

    companion object {
        /** The final byte of a designation and the set it picks. */
        val DESIGNATORS: Map<Char, Charset> = mapOf('B' to ASCII, 'A' to ASCII, 'U' to ASCII, '0' to LINE_DRAWING)
    }
}
