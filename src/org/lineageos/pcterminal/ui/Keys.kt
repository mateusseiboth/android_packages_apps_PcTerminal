package org.lineageos.pcterminal.ui

import android.view.KeyCharacterMap
import android.view.KeyEvent
import org.lineageos.pcterminal.core.TerminalSession
import org.lineageos.pcterminal.core.terminal.KeyModifiers
import org.lineageos.pcterminal.core.terminal.TerminalKey

/** Window actions on Ctrl+Shift and Ctrl+PageUp/PageDown, as other terminals have them. */
enum class Shortcut { PASTE, NEW_TAB, CLOSE_TAB, NEXT_TAB, PREVIOUS_TAB, SCROLL_UP, SCROLL_DOWN }

private val CTRL_SHIFT_SHORTCUTS = mapOf(
    KeyEvent.KEYCODE_V to Shortcut.PASTE,
    KeyEvent.KEYCODE_T to Shortcut.NEW_TAB,
    KeyEvent.KEYCODE_W to Shortcut.CLOSE_TAB,
)

private val CTRL_SHORTCUTS = mapOf(
    KeyEvent.KEYCODE_PAGE_DOWN to Shortcut.NEXT_TAB,
    KeyEvent.KEYCODE_PAGE_UP to Shortcut.PREVIOUS_TAB,
    KeyEvent.KEYCODE_TAB to Shortcut.NEXT_TAB,
)

private val SHIFT_SHORTCUTS = mapOf(
    KeyEvent.KEYCODE_PAGE_UP to Shortcut.SCROLL_UP,
    KeyEvent.KEYCODE_PAGE_DOWN to Shortcut.SCROLL_DOWN,
)

/** Shortcut tables by (ctrl, shift). */
private val SHORTCUTS = mapOf(
    (true to true) to CTRL_SHIFT_SHORTCUTS,
    (true to false) to CTRL_SHORTCUTS,
    (false to true) to SHIFT_SHORTCUTS,
)

private val SPECIAL_KEYS = mapOf(
    KeyEvent.KEYCODE_DPAD_UP to TerminalKey.UP,
    KeyEvent.KEYCODE_DPAD_DOWN to TerminalKey.DOWN,
    KeyEvent.KEYCODE_DPAD_LEFT to TerminalKey.LEFT,
    KeyEvent.KEYCODE_DPAD_RIGHT to TerminalKey.RIGHT,
    KeyEvent.KEYCODE_MOVE_HOME to TerminalKey.HOME,
    KeyEvent.KEYCODE_MOVE_END to TerminalKey.END,
    KeyEvent.KEYCODE_PAGE_UP to TerminalKey.PAGE_UP,
    KeyEvent.KEYCODE_PAGE_DOWN to TerminalKey.PAGE_DOWN,
    KeyEvent.KEYCODE_INSERT to TerminalKey.INSERT,
    KeyEvent.KEYCODE_FORWARD_DEL to TerminalKey.DELETE,
    KeyEvent.KEYCODE_DEL to TerminalKey.BACKSPACE,
    KeyEvent.KEYCODE_TAB to TerminalKey.TAB,
    KeyEvent.KEYCODE_ENTER to TerminalKey.ENTER,
    KeyEvent.KEYCODE_NUMPAD_ENTER to TerminalKey.ENTER,
    KeyEvent.KEYCODE_ESCAPE to TerminalKey.ESCAPE,
    KeyEvent.KEYCODE_F1 to TerminalKey.F1,
    KeyEvent.KEYCODE_F2 to TerminalKey.F2,
    KeyEvent.KEYCODE_F3 to TerminalKey.F3,
    KeyEvent.KEYCODE_F4 to TerminalKey.F4,
    KeyEvent.KEYCODE_F5 to TerminalKey.F5,
    KeyEvent.KEYCODE_F6 to TerminalKey.F6,
    KeyEvent.KEYCODE_F7 to TerminalKey.F7,
    KeyEvent.KEYCODE_F8 to TerminalKey.F8,
    KeyEvent.KEYCODE_F9 to TerminalKey.F9,
    KeyEvent.KEYCODE_F10 to TerminalKey.F10,
    KeyEvent.KEYCODE_F11 to TerminalKey.F11,
    KeyEvent.KEYCODE_F12 to TerminalKey.F12,
)

/** Hardware keys of one terminal: shortcuts, then special keys, then typed characters with dead keys. */
class KeyHandler(private val onShortcut: (Shortcut) -> Unit) {
    private var deadAccent = 0

    /** True when the key was used. */
    fun onKeyDown(event: KeyEvent, session: TerminalSession): Boolean {
        val modifiers = KeyModifiers(shift = event.isShiftPressed, alt = event.isAltPressed, ctrl = event.isCtrlPressed)
        findShortcut(event, modifiers)?.let {
            onShortcut(it)
            return true
        }
        SPECIAL_KEYS[event.keyCode]?.let {
            session.sendKey(it, modifiers)
            return true
        }
        return sendCharacter(event, modifiers, session)
    }

    private fun findShortcut(event: KeyEvent, m: KeyModifiers): Shortcut? =
        SHORTCUTS[m.ctrl to m.shift]?.get(event.keyCode)?.takeUnless { m.alt }

    private fun sendCharacter(event: KeyEvent, modifiers: KeyModifiers, session: TerminalSession): Boolean {
        val meta = event.metaState and (KeyEvent.META_CTRL_MASK or KeyEvent.META_ALT_MASK).inv()
        val unicode = event.getUnicodeChar(meta)
        if (unicode == 0) return false
        if (unicode and KeyCharacterMap.COMBINING_ACCENT != 0) {
            deadAccent = unicode and KeyCharacterMap.COMBINING_ACCENT_MASK
            return true
        }
        val composed = deadAccent.takeIf { it != 0 }?.let { KeyCharacterMap.getDeadChar(it, unicode) }?.takeIf { it != 0 } ?: unicode
        deadAccent = 0
        session.sendText(String(Character.toChars(composed)), modifiers)
        return true
    }
}
