#pragma once
// Single GL entry point for the whole engine.
// Desktop: GL 3.3 core through glad (function pointers loaded at runtime).
// Web: WebGL2 == GLES 3.0. Emscripten links the GL functions directly, so there
// is nothing to load and glad must not be compiled in at all (it would pull in
// desktop-only enums/functions that silently don't exist in WebGL2).
#ifdef __EMSCRIPTEN__
    #include <GLES3/gl3.h>
#else
    #include <glad/glad.h>
#endif
