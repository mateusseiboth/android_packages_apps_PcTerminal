package org.lineageos.pcterminal

import android.net.LocalSocket
import android.net.LocalSocketAddress
import android.system.Os
import java.io.ByteArrayOutputStream
import java.io.Closeable
import java.io.FileDescriptor
import java.io.IOException
import org.lineageos.pcterminal.core.ContainerClient
import org.lineageos.pcterminal.core.ContainerError
import org.lineageos.pcterminal.core.DaemonResult
import org.lineageos.pcterminal.core.Transport

/** One connection to /dev/socket/pc-containerd; fds ride on the lines both ways (SCM_RIGHTS). */
class LocalSocketTransport(private val socket: LocalSocket) : Transport<FileDescriptor>, Closeable {
    private val input = socket.inputStream
    private val pending = ByteArrayOutputStream()
    private val buffer = ByteArray(BUFFER)
    private var buffered = 0
    private val received = ArrayDeque<FileDescriptor>()

    override fun send(line: String, fd: FileDescriptor?) {
        socket.setFileDescriptorsForSend(fd?.let { arrayOf(it) })
        socket.outputStream.write(line.toByteArray(Charsets.UTF_8))
        socket.outputStream.flush()
        socket.setFileDescriptorsForSend(null)
    }

    override fun readLine(): String? {
        while (true) {
            val newline = (0 until buffered).firstOrNull { buffer[it] == '\n'.code.toByte() }
            if (newline != null) return takeLine(newline)
            if (buffered == buffer.size) {
                pending.write(buffer, 0, buffered)
                buffered = 0
            }
            val n = input.read(buffer, buffered, buffer.size - buffered)
            if (n < 0) return null
            socket.ancillaryFileDescriptors?.let { received.addAll(it) }
            buffered += n
        }
    }

    override fun takeFd(): FileDescriptor? = received.removeFirstOrNull()

    override fun close() {
        received.forEach { runCatching { Os.close(it) } }
        socket.close()
    }

    private fun takeLine(newline: Int): String {
        pending.write(buffer, 0, newline)
        val line = pending.toString(Charsets.UTF_8.name())
        pending.reset()
        System.arraycopy(buffer, newline + 1, buffer, 0, buffered - newline - 1)
        buffered -= newline + 1
        return line
    }

    private companion object {
        const val BUFFER = 64 * 1024
    }
}

/** A connection per task; pc-containerd runs from boot. */
object DaemonConnection {
    private const val SOCKET = "pc-containerd"

    fun <T> withClient(block: (ContainerClient<FileDescriptor>) -> DaemonResult<T>): DaemonResult<T> {
        val transport = connect() ?: return DaemonResult.Failed(ContainerError.UNREACHABLE)
        return transport.use { block(ContainerClient(it)) }
    }

    private fun connect(): LocalSocketTransport? {
        val socket = LocalSocket()
        return try {
            socket.connect(LocalSocketAddress(SOCKET, LocalSocketAddress.Namespace.RESERVED))
            LocalSocketTransport(socket)
        } catch (e: IOException) {
            socket.close()
            null
        }
    }
}
