package org.lineageos.pcterminal.core

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test

/** Answers each request with the next scripted lines and remembers what was sent. */
class ScriptedTransport(vararg replies: List<Pair<String, String?>>) : Transport<String> {
    private val pending = replies.toMutableList()
    private val lines = ArrayDeque<Pair<String, String?>>()
    private var lastFd: String? = null
    val sent = mutableListOf<Pair<String, String?>>()

    override fun send(line: String, fd: String?) {
        sent += line to fd
        lines.addAll(pending.removeAt(0))
    }

    override fun readLine(): String? = lines.removeFirstOrNull()?.also { lastFd = it.second }?.first

    override fun takeFd(): String? = lastFd.also { lastFd = null }
}

private fun reply(line: String, fd: String? = null) = listOf(line to fd)

class ContainerClientTest {
    @Test
    fun listsContainers() {
        val transport = ScriptedTransport(reply("""{"id":1,"ok":true,"containers":[{"name":"debian","distro":"debian","release":"trixie","created":1764072000,"user":"mateus","state":"running"}]}"""))
        val result = ContainerClient(transport).list()
        assertEquals("""{"id":1,"cmd":"list"}""" + "\n", transport.sent.single().first)
        assertEquals(
            DaemonResult.Ok(listOf(Container("debian", "debian", "trixie", 1764072000, "mateus", ContainerState.RUNNING))),
            result,
        )
    }

    @Test
    fun createsWithTheStreamAttachedAndReportsProgress() {
        val transport = ScriptedTransport(
            listOf(
                """{"id":1,"event":"progress","bytes":100,"files":2}""" to null,
                """{"id":1,"ok":true,"container":{"name":"box","distro":"alpine","release":"3.24","created":5,"user":"user","state":"stopped"}}""" to null,
            ),
        )
        val progress = mutableListOf<Pair<Long, Long>>()
        val result = ContainerClient(transport).create("box", "alpine", "3.24", "user", "PIPE") { bytes, files -> progress += bytes to files }
        assertEquals("PIPE", transport.sent.single().second)
        assertEquals("""{"id":1,"cmd":"create","name":"box","distro":"alpine","release":"3.24","user":"user"}""" + "\n", transport.sent.single().first)
        assertEquals(listOf(100L to 2L), progress)
        assertEquals("box", (result as DaemonResult.Ok).value.name)
    }

    @Test
    fun opensAShellAndTakesThePty() {
        val transport = ScriptedTransport(reply("""{"id":1,"ok":true,"pid":7,"fd":true}""", "PTY"))
        val result = ContainerClient(transport).openShell("debian", rows = 30, cols = 100)
        assertEquals("""{"id":1,"cmd":"exec","name":"debian","pty":true,"rows":30,"cols":100}""" + "\n", transport.sent.single().first)
        assertEquals(DaemonResult.Ok(Shell(7, "PTY")), result)
    }

    @Test
    fun aRootShellAsksForRoot() {
        val transport = ScriptedTransport(reply("""{"id":1,"ok":true,"pid":7,"fd":true}""", "PTY"))
        ContainerClient(transport).openShell("debian", 24, 80, asRoot = true)
        assertEquals("""{"id":1,"cmd":"exec","name":"debian","pty":true,"rows":24,"cols":80,"user":"root"}""" + "\n", transport.sent.single().first)
    }

    @Test
    fun mapsErrorCodes() {
        val transport = ScriptedTransport(reply("""{"id":1,"ok":false,"error":"exists","message":"x"}"""), reply("""{"id":2,"ok":false,"error":"weird"}"""))
        val client = ContainerClient(transport)
        assertEquals(DaemonResult.Failed<Container>(ContainerError.EXISTS), client.rename("a", "b"))
        assertEquals(DaemonResult.Failed<Unit>(ContainerError.IO), client.stop("a"))
    }

    @Test
    fun aDaemonThatHangsUpIsUnreachable() {
        val transport = ScriptedTransport(emptyList())
        assertEquals(DaemonResult.Failed<Unit>(ContainerError.UNREACHABLE), ContainerClient(transport).start("a", emptyList()))
    }

    @Test
    fun startsWithNameServers() {
        val transport = ScriptedTransport(reply("""{"id":1,"ok":true,"state":"running"}"""))
        ContainerClient(transport).start("a", listOf("192.168.0.1"))
        assertEquals("""{"id":1,"cmd":"start","name":"a","dns":["192.168.0.1"]}""" + "\n", transport.sent.single().first)
    }

    @Test
    fun readsStatsWithMissingNumbers() {
        val transport = ScriptedTransport(reply("""{"id":1,"ok":true,"state":"stopped","memory_bytes":null,"cpu_usec":null,"pids":null,"disk_bytes":4096}"""))
        val stats = (ContainerClient(transport).stats("a") as DaemonResult.Ok).value
        assertEquals(ContainerState.STOPPED, stats.state)
        assertNull(stats.memoryBytes)
        assertEquals(4096L, stats.diskBytes)
    }

    @Test
    fun resizesWithThePtyAttached() {
        val transport = ScriptedTransport(reply("""{"id":1,"ok":true}"""))
        ContainerClient(transport).resize("PTY", 50, 132)
        assertEquals("""{"id":1,"cmd":"resize","rows":50,"cols":132}""" + "\n" to "PTY", transport.sent.single())
    }
}
