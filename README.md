# GuiKit Native Backend (`guikit::native`)

GuiKit's own lightweight native platform windowing (`Win32`, `X11`, `Wayland`, `Emscripten`) and hardware-accelerated **OpenGL ES 3.0 / GL 3.3 Core / WebGL2** 2D vector & GLSL shader backend.

Zero **SDL3**, zero **ThorVG**, and zero **FreeType** dependencies — built directly on `guikit::core` (`IPlatformWindow`, `PlatformWindowBackend`, `ICanvas`) and `guikit::text` (defaulting to the zero-dependency `keel::font` TrueType/OpenType rasterizer, with optional FreeType support).

## License

Licensed under **0BSD** (`LICENSE`).
