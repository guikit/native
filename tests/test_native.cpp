#include "guikit/native.hpp"
#include "keel/keel.h"

static keel::i32 g_failures = 0;

#define CHECK(cond)                                                                  \
    do {                                                                             \
        if (!(cond)) {                                                               \
            keel::eprintln("[FAIL] {} ({}:{})", #cond, __FILE__, __LINE__);          \
            ++g_failures;                                                            \
        }                                                                            \
    } while (0)

int main() {
    keel::println("[RUN] Testing GuiKit Native backend module...");

    CHECK(guikit::native::IsPlatformCompiled(guikit::native::NativePlatform::Headless));
    CHECK(guikit::native::IsRendererCompiled(guikit::native::NativeRenderer::Null));
    CHECK(!guikit::native::GetBackendSummary().empty());

    if (g_failures == 0) {
        keel::println("[PASS] All GuiKit Native tests passed successfully!");
        return 0;
    }
    keel::eprintln("[FAIL] Total failures: {}", g_failures);
    return 1;
}
