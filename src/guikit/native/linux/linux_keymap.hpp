#pragma once

#include "guikit/core/types.hpp"
#include "keel/core.h"

namespace guikit::native::linux_detail {

/// Maps an X11 KeySym / XKB keysym (identical numeric standard) to a GuiKit logical `Key`.
[[nodiscard]] inline Key MapLinuxKeySym(keel::u32 sym) noexcept {
    switch (sym) {
        case 0xFF08u: return Key::Backspace; // XK_BackSpace
        case 0xFFFFu:
        case 0xFF9Fu: return Key::Delete;    // XK_Delete / XK_KP_Delete
        case 0xFF0Du:
        case 0xFF8Du: return Key::Enter;     // XK_Return / XK_KP_Enter
        case 0xFF1Bu: return Key::Escape;    // XK_Escape
        case 0xFF09u:
        case 0xFE20u: return Key::Tab;       // XK_Tab / XK_ISO_Left_Tab
        case 0x0020u: return Key::Space;     // XK_space
        case 0xFF51u:
        case 0xFF96u: return Key::Left;      // XK_Left / XK_KP_Left
        case 0xFF53u:
        case 0xFF98u: return Key::Right;     // XK_Right / XK_KP_Right
        case 0xFF50u:
        case 0xFF95u: return Key::Home;      // XK_Home / XK_KP_Home
        case 0xFF57u:
        case 0xFF9Cu: return Key::End;       // XK_End / XK_KP_End
        case 0xFF52u:
        case 0xFF97u: return Key::Up;        // XK_Up / XK_KP_Up
        case 0xFF54u:
        case 0xFF99u: return Key::Down;      // XK_Down / XK_KP_Down
        case 0xFF55u:
        case 0xFF9Au: return Key::PageUp;    // XK_Page_Up / XK_KP_Page_Up
        case 0xFF56u:
        case 0xFF9Bu: return Key::PageDown;  // XK_Page_Down / XK_KP_Page_Down
        case 'a': case 'A': return Key::A;
        case 'c': case 'C': return Key::C;
        case 'd': case 'D': return Key::D;
        case 'f': case 'F': return Key::F;
        case 'g': case 'G': return Key::G;
        case 'h': case 'H': return Key::H;
        case 'k': case 'K': return Key::K;
        case 'v': case 'V': return Key::V;
        case 'x': case 'X': return Key::X;
        case 'y': case 'Y': return Key::Y;
        case 'z': case 'Z': return Key::Z;
        case '/': case 0xFFAFu: return Key::Slash;    // XK_slash / XK_KP_Divide
        case '+': case '=': case 0xFFABu: return Key::Plus;  // XK_plus / XK_equal / XK_KP_Add
        case '-': case 0xFFADu: return Key::Minus;    // XK_minus / XK_KP_Subtract
        case '0': case 0xFFB0u: return Key::Zero;     // XK_0 / XK_KP_0
        case 0xFFC0u: return Key::F3;                 // XK_F3
        case 0xFFC2u: return Key::F5;                 // XK_F5
        case 0xFFC6u: return Key::F9;                 // XK_F9
        case 0xFFC7u: return Key::F10;                // XK_F10
        case 0xFFC8u: return Key::F11;                // XK_F11
        default: return Key::None;
    }
}

} // namespace guikit::native::linux_detail
