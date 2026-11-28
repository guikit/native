#include "linux_desktop_services.hpp"

#include "keel/array.h"

#if defined(__linux__) && !defined(__ANDROID__)
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace guikit::native::linux_detail {

#if defined(__linux__) && !defined(__ANDROID__)

namespace {

struct ExecCaptureResult {
    bool launched{false};
    keel::i32 exitCode{-1};
    keel::String output;
};

ExecCaptureResult RunExternalDialogCommand(const keel::Array<keel::String>& args) {
    ExecCaptureResult res{};
    if (args.empty()) return res;

    int pipeFds[2]{-1, -1}; // keel-lint: ignore
    if (::pipe(pipeFds) != 0) {
        return res;
    }

    keel::Array<char*> argv;
    argv.reserve(args.size() + 1u);
    for (const keel::String& s : args) {
        argv.push_back(const_cast<char*>(s.c_str()));
    }
    argv.push_back(nullptr);

    const pid_t pid = ::fork();
    if (pid < 0) {
        ::close(pipeFds[0]);
        ::close(pipeFds[1]);
        return res;
    }
    if (pid == 0) {
        ::close(pipeFds[0]);
        (void)::dup2(pipeFds[1], STDOUT_FILENO);
        ::close(pipeFds[1]);
        const int devNull = ::open("/dev/null", O_WRONLY); // keel-lint: ignore
        if (devNull >= 0) {
            (void)::dup2(devNull, STDERR_FILENO);
            ::close(devNull);
        }
        ::execvp(argv[0], argv.data());
        ::_exit(127);
    }

    ::close(pipeFds[1]);
    char buf[1024]{};
    while (true) {
        const keel::isize n = ::read(pipeFds[0], buf, sizeof(buf));
        if (n <= 0) break;
        res.output.append(keel::StringRef(buf, keel::as<keel::usize>(n)));
    }
    ::close(pipeFds[0]);

    int status = 0; // keel-lint: ignore
    (void)::waitpid(pid, &status, 0);
    if (WIFEXITED(status)) {
        res.exitCode = WEXITSTATUS(status);
        res.launched = (res.exitCode != 127);
    }
    return res;
}

void SplitOutputLines(const keel::String& text, keel::Array<keel::String>& outPaths) {
    keel::usize pos = 0u;
    while (pos < text.size()) {
        keel::usize nl = text.find('\n', pos);
        if (nl == keel::StringRef::npos) nl = text.size();
        const keel::StringRef line = text.substr(pos, nl - pos).trim();
        if (!line.empty()) {
            outPaths.push_back(keel::String::from(line));
        }
        pos = (nl < text.size()) ? (nl + 1u) : text.size();
    }
}

} // namespace

void OpenLinuxUrl(keel::StringRef url) noexcept {
    if (url.empty()) return;
    const keel::String urlStr = keel::String::from(url);
    const pid_t mid = ::fork();
    if (mid == 0) {
        const pid_t leaf = ::fork();
        if (leaf == 0) {
            ::execlp("xdg-open", "xdg-open", urlStr.c_str(), static_cast<char*>(nullptr));
            ::_exit(127);
        }
        ::_exit(0);
    }
    if (mid > 0) {
        int status = 0; // keel-lint: ignore
        (void)::waitpid(mid, &status, 0);
    }
}

FileDialogResult ShowLinuxFileDialog(const FileDialogOptions& opts) {
    FileDialogResult result{};
    result.supported = false;
    result.accepted = false;

    // 1. Try zenity first (GNOME/GTK ecosystems)
    {
        keel::Array<keel::String> args;
        args.push_back(keel::String("zenity"));
        args.push_back(keel::String("--file-selection"));
        if (!opts.title.empty()) {
            args.push_back(keel::String("--title"));
            args.push_back(keel::String::from(opts.title));
        }
        if (opts.kind == FileDialogKind::OpenFiles) {
            args.push_back(keel::String("--multiple"));
            args.push_back(keel::String("--separator=\n"));
        } else if (opts.kind == FileDialogKind::SaveFile) {
            args.push_back(keel::String("--save"));
            args.push_back(keel::String("--confirm-overwrite"));
        } else if (opts.kind == FileDialogKind::SelectFolder) {
            args.push_back(keel::String("--directory"));
        }
        if (!opts.defaultPath.empty() || !opts.defaultName.empty()) {
            keel::String fn = keel::String::from(opts.defaultPath);
            if (!opts.defaultName.empty()) {
                if (!fn.empty() && fn.back() != '/') fn.push_back('/');
                fn.append(opts.defaultName);
            }
            if (!fn.empty()) {
                args.push_back(keel::String("--filename"));
                args.push_back(keel::move(fn));
            }
        }
        if (opts.kind != FileDialogKind::SelectFolder) {
            for (const FileDialogFilter& f : opts.filters) {
                if (f.pattern.empty()) continue;
                keel::String spec = keel::String::from(f.name.empty() ? f.pattern : f.name);
                spec.append(" | ");
                for (keel::usize i = 0u; i < f.pattern.size(); ++i) {
                    spec.push_back(f.pattern[i] == ';' ? ' ' : f.pattern[i]);
                }
                args.push_back(keel::String("--file-filter"));
                args.push_back(keel::move(spec));
            }
        }

        const ExecCaptureResult exec = RunExternalDialogCommand(args);
        if (exec.launched) {
            result.supported = true;
            if (exec.exitCode == 0) {
                SplitOutputLines(exec.output, result.paths);
                result.accepted = !result.paths.empty();
            }
            return result;
        }
    }

    // 2. Fallback to kdialog (KDE/Qt ecosystems)
    {
        keel::Array<keel::String> args;
        args.push_back(keel::String("kdialog"));
        if (!opts.title.empty()) {
            args.push_back(keel::String("--title"));
            args.push_back(keel::String::from(opts.title));
        }
        switch (opts.kind) {
            case FileDialogKind::OpenFile:
                args.push_back(keel::String("--getopenfilename"));
                break;
            case FileDialogKind::OpenFiles:
                args.push_back(keel::String("--getopenfilename"));
                args.push_back(keel::String("--multiple"));
                args.push_back(keel::String("--separate-output"));
                break;
            case FileDialogKind::SaveFile:
                args.push_back(keel::String("--getsavefilename"));
                break;
            case FileDialogKind::SelectFolder:
                args.push_back(keel::String("--getexistingdirectory"));
                break;
        }
        if (!opts.defaultPath.empty()) {
            args.push_back(keel::String::from(opts.defaultPath));
        } else {
            args.push_back(keel::String("."));
        }

        const ExecCaptureResult exec = RunExternalDialogCommand(args);
        if (exec.launched) {
            result.supported = true;
            if (exec.exitCode == 0) {
                SplitOutputLines(exec.output, result.paths);
                result.accepted = !result.paths.empty();
            }
            return result;
        }
    }

    return result;
}

MessageBoxChoice ShowLinuxMessageBox(const MessageBoxOptions& opts) {
    keel::Array<keel::String> args;
    args.push_back(keel::String("zenity"));
    if (opts.buttons == MessageBoxButtons::Ok) {
        switch (opts.icon) {
            case MessageBoxIcon::Warning: args.push_back(keel::String("--warning")); break;
            case MessageBoxIcon::Error:   args.push_back(keel::String("--error")); break;
            default:                      args.push_back(keel::String("--info")); break;
        }
    } else {
        args.push_back(keel::String("--question"));
    }
    if (!opts.title.empty()) {
        args.push_back(keel::String("--title"));
        args.push_back(keel::String::from(opts.title));
    }
    args.push_back(keel::String("--text"));
    args.push_back(keel::String::from(opts.message));

    const ExecCaptureResult exec = RunExternalDialogCommand(args);
    if (!exec.launched) {
        return MessageBoxChoice::Cancel;
    }
    if (exec.exitCode == 0) {
        return (opts.buttons == MessageBoxButtons::YesNo || opts.buttons == MessageBoxButtons::YesNoCancel)
            ? MessageBoxChoice::Yes
            : MessageBoxChoice::Ok;
    }
    return (opts.buttons == MessageBoxButtons::YesNo) ? MessageBoxChoice::No : MessageBoxChoice::Cancel;
}

#else

void OpenLinuxUrl(keel::StringRef) noexcept {}
FileDialogResult ShowLinuxFileDialog(const FileDialogOptions&) { return {.accepted = false, .supported = false, .paths = {}}; }
MessageBoxChoice ShowLinuxMessageBox(const MessageBoxOptions&) { return MessageBoxChoice::Cancel; }

#endif

} // namespace guikit::native::linux_detail
