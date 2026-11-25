package org.lineageos.pcterminal.core

/**
 * The daemon's JSON lines (containerd/PROTOCOL.md): objects become Map<String, Any?>, arrays List<Any?>,
 * integers Long, other numbers Double.
 */
object Json {
    fun parse(text: String): Any? = parseOrNull(text) ?: if (text.trim() == "null") null else throw IllegalArgumentException("not JSON")

    fun parseOrNull(text: String): Any? = runCatching {
        val reader = Reader(text)
        val value = reader.readValue()
        reader.skipSpaces()
        require(reader.isAtEnd)
        value
    }.getOrNull()

    fun write(value: Any?): String = StringBuilder().also { appendValue(it, value) }.toString()

    private fun appendValue(out: StringBuilder, value: Any?) {
        when (value) {
            null -> out.append("null")
            is String -> appendString(out, value)
            is Boolean, is Int, is Long -> out.append(value.toString())
            is Number -> out.append(value.toDouble().toString())
            is Map<*, *> -> appendObject(out, value)
            is List<*> -> appendArray(out, value)
            else -> appendString(out, value.toString())
        }
    }

    private fun appendObject(out: StringBuilder, map: Map<*, *>) {
        out.append('{')
        map.entries.forEachIndexed { i, (key, value) ->
            if (i > 0) out.append(',')
            appendString(out, key.toString())
            out.append(':')
            appendValue(out, value)
        }
        out.append('}')
    }

    private fun appendArray(out: StringBuilder, list: List<*>) {
        out.append('[')
        list.forEachIndexed { i, value ->
            if (i > 0) out.append(',')
            appendValue(out, value)
        }
        out.append(']')
    }

    private val ESCAPES = mapOf('"' to "\\\"", '\\' to "\\\\", '\n' to "\\n", '\r' to "\\r", '\t' to "\\t", '\b' to "\\b", '\u000c' to "\\f")

    private fun appendString(out: StringBuilder, s: String) {
        out.append('"')
        s.forEach { c ->
            val escape = ESCAPES[c]
            when {
                escape != null -> out.append(escape)
                c < ' ' -> out.append("\\u%04x".format(c.code))
                else -> out.append(c)
            }
        }
        out.append('"')
    }

    private class Reader(private val text: String) {
        private var at = 0
        private var depth = 0
        val isAtEnd get() = at == text.length

        fun skipSpaces() {
            while (at < text.length && text[at] in " \t\r\n") at++
        }

        fun readValue(): Any? {
            skipSpaces()
            require(at < text.length)
            return when (text[at]) {
                '{' -> readObject()
                '[' -> readArray()
                '"' -> readString()
                't' -> readWord("true", true)
                'f' -> readWord("false", false)
                'n' -> readWord("null", null)
                else -> readNumber()
            }
        }

        private fun readWord(word: String, value: Any?): Any? {
            require(text.startsWith(word, at))
            at += word.length
            return value
        }

        private fun readNumber(): Any {
            val start = at
            if (text.getOrNull(at) == '-') at++
            require(text.getOrNull(at)?.isDigit() == true)
            while (at < text.length && (text[at].isDigit() || text[at] in ".eE+-")) at++
            val number = text.substring(start, at)
            return number.toLongOrNull() ?: number.toDouble()
        }

        private fun readString(): String {
            at++
            val out = StringBuilder()
            while (true) {
                require(at < text.length)
                val c = text[at++]
                when {
                    c == '"' -> return out.toString()
                    c == '\\' -> out.append(readEscape())
                    else -> {
                        require(c >= ' ')
                        out.append(c)
                    }
                }
            }
        }

        private val SIMPLE = mapOf('"' to '"', '\\' to '\\', '/' to '/', 'b' to '\b', 'f' to '\u000c', 'n' to '\n', 'r' to '\r', 't' to '\t')

        private fun readEscape(): String {
            require(at < text.length)
            val c = text[at++]
            SIMPLE[c]?.let { return it.toString() }
            require(c == 'u' && at + 4 <= text.length)
            val unit = text.substring(at, at + 4).toInt(16)
            at += 4
            require(!Character.isLowSurrogate(unit.toChar()))
            if (!Character.isHighSurrogate(unit.toChar())) return unit.toChar().toString()
            require(text.startsWith("\\u", at) && at + 6 <= text.length)
            val low = text.substring(at + 2, at + 6).toInt(16).toChar()
            require(Character.isLowSurrogate(low))
            at += 6
            return String(charArrayOf(unit.toChar(), low))
        }

        private fun <T> readContainer(close: Char, readItem: () -> T): List<T> {
            require(++depth <= MAX_DEPTH)
            at++
            val items = mutableListOf<T>()
            skipSpaces()
            if (text.getOrNull(at) == close) {
                at++
                depth--
                return items
            }
            while (true) {
                items += readItem()
                skipSpaces()
                val c = text.getOrNull(at++)
                if (c == close) break
                require(c == ',')
            }
            depth--
            return items
        }

        private fun readArray(): List<Any?> = readContainer(']') { readValue() }

        private fun readObject(): Map<String, Any?> = readContainer('}') {
            skipSpaces()
            require(text.getOrNull(at) == '"')
            val key = readString()
            skipSpaces()
            require(text.getOrNull(at++) == ':')
            key to readValue()
        }.toMap(LinkedHashMap())

        companion object {
            const val MAX_DEPTH = 32
        }
    }
}
