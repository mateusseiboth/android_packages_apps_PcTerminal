package org.lineageos.pcterminal.core

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test

class JsonTest {
    @Test
    fun parsesObjectsArraysAndScalars() {
        val value = Json.parse("""{"id":7,"ok":true,"list":[1,"a",null,false],"o":{"x":-2.5},"s":"\u00e9\n\"q\""}""")
        val map = value as Map<*, *>
        assertEquals(7L, map["id"])
        assertEquals(true, map["ok"])
        assertEquals(listOf(1L, "a", null, false), map["list"])
        assertEquals(-2.5, (map["o"] as Map<*, *>)["x"])
        assertEquals("é\n\"q\"", map["s"])
    }

    @Test
    fun decodesSurrogatePairs() {
        assertEquals("😀", Json.parse("\"\\ud83d\\ude00\""))
    }

    @Test
    fun rejectsMalformedText() {
        listOf("", "{", "{\"a\":}", "[1,]", "tru", "{\"a\":1} x", "\"\\x\"").forEach {
            assertNull(it, Json.parseOrNull(it))
        }
    }

    @Test
    fun writesWhatItReads() {
        val text = Json.write(mapOf("id" to 3, "cmd" to "exec", "argv" to listOf("ls", "-l"), "env" to mapOf("A" to "b\"c"), "pty" to true, "x" to null))
        assertEquals("""{"id":3,"cmd":"exec","argv":["ls","-l"],"env":{"A":"b\"c"},"pty":true,"x":null}""", text)
        assertEquals("\"\\u0001\\n\\t\\\\\"", Json.write("\u0001\n\t\\"))
    }
}
