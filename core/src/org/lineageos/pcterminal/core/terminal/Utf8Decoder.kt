package org.lineageos.pcterminal.core.terminal

/**
 * Turns a byte stream into Unicode code points. A sequence split across two [decode] calls is
 * completed by the next call; bytes that are not valid UTF-8 become U+FFFD.
 */
class Utf8Decoder {
    private var codePoint = 0
    private var remaining = 0
    private var minimum = 0

    fun decode(bytes: ByteArray, offset: Int, length: Int, onCodePoint: (Int) -> Unit) {
        for (i in offset until offset + length) {
            decodeByte(bytes[i].toInt() and 0xFF, onCodePoint)
        }
    }

    private fun decodeByte(byte: Int, emit: (Int) -> Unit) {
        if (remaining > 0) {
            continueSequence(byte, emit)
            return
        }
        startSequence(byte, emit)
    }

    private fun continueSequence(byte: Int, emit: (Int) -> Unit) {
        if (byte and 0xC0 != 0x80) {
            remaining = 0
            emit(REPLACEMENT)
            startSequence(byte, emit)
            return
        }
        codePoint = codePoint shl 6 or (byte and 0x3F)
        remaining--
        if (remaining == 0) emit(validate(codePoint))
    }

    private fun validate(value: Int): Int = when {
        value < minimum -> REPLACEMENT
        value in SURROGATES -> REPLACEMENT
        value > MAX_CODE_POINT -> REPLACEMENT
        else -> value
    }

    private fun startSequence(byte: Int, emit: (Int) -> Unit) {
        when (byte) {
            in 0x00..0x7F -> emit(byte)
            in 0xC2..0xDF -> begin(byte and 0x1F, 1, 0x80)
            in 0xE0..0xEF -> begin(byte and 0x0F, 2, 0x800)
            in 0xF0..0xF4 -> begin(byte and 0x07, 3, 0x10000)
            else -> emit(REPLACEMENT)
        }
    }

    private fun begin(bits: Int, continuation: Int, smallest: Int) {
        codePoint = bits
        remaining = continuation
        minimum = smallest
    }

    companion object {
        const val REPLACEMENT = 0xFFFD
        private const val MAX_CODE_POINT = 0x10FFFF
        private val SURROGATES = 0xD800..0xDFFF
    }
}
