package org.lineageos.pcterminal.core

import java.io.IOException

/**
 * One connection to pc-containerd. [F] is how the platform holds a file descriptor (FileDescriptor on
 * Android). Implementations block; call them off the main thread.
 */
interface Transport<F> {
    /** Sends one line, with fd attached to its bytes when not null. */
    fun send(line: String, fd: F?)

    /** The next line without its newline, or null when the daemon hung up. */
    fun readLine(): String?

    /** The fd that came with the last line read, once. */
    fun takeFd(): F?
}

/** The requests of containerd/PROTOCOL.md. */
class ContainerClient<F>(private val transport: Transport<F>) {
    private var nextId = 1L

    fun list(): DaemonResult<List<Container>> = request(mapOf("cmd" to "list")) { reply ->
        (reply["containers"] as? List<*>).orEmpty().mapNotNull { (it as? Map<*, *>)?.toContainer() }
    }

    fun create(name: String, distro: String, release: String, user: String, source: F, onProgress: (Long, Long) -> Unit):
        DaemonResult<Container> = request(
            mapOf("cmd" to "create", "name" to name, "distro" to distro, "release" to release, "user" to user),
            fd = source,
            onEvent = { onProgress(it.long("bytes") ?: 0, it.long("files") ?: 0) },
        ) { reply -> reply.containerOf() }

    fun start(name: String, dns: List<String>): DaemonResult<Unit> =
        request(mapOf("cmd" to "start", "name" to name) + (if (dns.isEmpty()) emptyMap() else mapOf("dns" to dns))) { }

    fun stop(name: String): DaemonResult<Unit> = request(mapOf("cmd" to "stop", "name" to name)) { }

    fun delete(name: String): DaemonResult<Unit> = request(mapOf("cmd" to "delete", "name" to name)) { }

    fun rename(name: String, newName: String): DaemonResult<Container> =
        request(mapOf("cmd" to "rename", "name" to name, "new_name" to newName)) { reply -> reply.containerOf() }

    fun openShell(name: String, rows: Int, cols: Int, asRoot: Boolean = false): DaemonResult<Shell<F>> {
        val user = if (asRoot) mapOf("user" to "root") else emptyMap()
        return request(mapOf("cmd" to "exec", "name" to name, "pty" to true, "rows" to rows, "cols" to cols) + user) { reply ->
            Shell(reply.long("pid") ?: 0, transport.takeFd() ?: throw IOException("no pty"))
        }
    }

    fun resize(pty: F, rows: Int, cols: Int): DaemonResult<Unit> =
        request(mapOf("cmd" to "resize", "rows" to rows, "cols" to cols), fd = pty) { }

    fun stats(name: String): DaemonResult<ContainerStats> = request(mapOf("cmd" to "stats", "name" to name)) { reply ->
        ContainerStats(
            ContainerState.fromCode(reply["state"]),
            reply.long("memory_bytes"),
            reply.long("cpu_usec"),
            reply.long("pids"),
            reply.long("disk_bytes"),
        )
    }

    private fun <T> request(
        fields: Map<String, Any?>,
        fd: F? = null,
        onEvent: (Map<*, *>) -> Unit = {},
        build: (Map<*, *>) -> T,
    ): DaemonResult<T> = try {
        val id = nextId++
        transport.send(Json.write(mapOf("id" to id) + fields) + "\n", fd)
        readReply(onEvent, build)
    } catch (e: IOException) {
        DaemonResult.Failed(ContainerError.UNREACHABLE)
    }

    private fun <T> readReply(onEvent: (Map<*, *>) -> Unit, build: (Map<*, *>) -> T): DaemonResult<T> {
        while (true) {
            val line = transport.readLine() ?: return DaemonResult.Failed(ContainerError.UNREACHABLE)
            val reply = Json.parseOrNull(line) as? Map<*, *> ?: return DaemonResult.Failed(ContainerError.IO)
            if (reply.containsKey("event")) {
                onEvent(reply)
                continue
            }
            if (reply["ok"] != true) return DaemonResult.Failed(ContainerError.fromCode(reply["error"]))
            return DaemonResult.Ok(build(reply))
        }
    }
}

private fun Map<*, *>.long(key: String): Long? = (this[key] as? Number)?.toLong()

private fun Map<*, *>.containerOf(): Container =
    (this["container"] as? Map<*, *>)?.toContainer() ?: throw IOException("no container in the reply")

private fun Map<*, *>.toContainer(): Container? {
    val name = this["name"] as? String ?: return null
    return Container(
        name,
        this["distro"] as? String ?: "",
        this["release"] as? String ?: "",
        long("created") ?: 0,
        this["user"] as? String ?: "",
        ContainerState.fromCode(this["state"]),
    )
}
