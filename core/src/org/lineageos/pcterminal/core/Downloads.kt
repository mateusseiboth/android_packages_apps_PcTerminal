package org.lineageos.pcterminal.core

import java.io.InputStream
import java.io.OutputStream
import java.security.MessageDigest

object Downloads {
    private const val BUFFER = 64 * 1024

    /** Copies everything, reporting the bytes copied so far; the SHA-256 of what was copied, in hex. */
    fun copyWithSha256(input: InputStream, output: OutputStream, onProgress: (Long) -> Unit): String {
        val digest = MessageDigest.getInstance("SHA-256")
        val buffer = ByteArray(BUFFER)
        var total = 0L
        while (true) {
            val n = input.read(buffer)
            if (n < 0) break
            digest.update(buffer, 0, n)
            output.write(buffer, 0, n)
            total += n
            onProgress(total)
        }
        return digest.digest().joinToString("") { "%02x".format(it) }
    }

    /** done / total from 0 to 1, or null when the total is unknown. */
    fun findFraction(done: Long, total: Long): Float? = if (total <= 0) null else (done.toFloat() / total).coerceAtMost(1f)
}
