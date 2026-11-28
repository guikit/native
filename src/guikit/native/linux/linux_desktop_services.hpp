#pragma once

#include "guikit/backend/system_dialogs.hpp"
#include "keel/string.h"

namespace guikit::native::linux_detail {

/// Launches the default desktop URL handler (`xdg-open`) without leaving zombie child processes.
void OpenLinuxUrl(keel::StringRef url) noexcept;

/// Opens a native Linux file dialog via `zenity` (GNOME/GTK) with `kdialog` (KDE/Qt) fallback.
[[nodiscard]] FileDialogResult ShowLinuxFileDialog(const FileDialogOptions& opts);

/// Opens a native Linux modal message box via `zenity`.
[[nodiscard]] MessageBoxChoice ShowLinuxMessageBox(const MessageBoxOptions& opts);

} // namespace guikit::native::linux_detail
