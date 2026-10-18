#pragma once

// The shaders are written for desktop GL 3.3; on OpenGL ES 3 (Android) the same text with the ES
// header (the language is the same, ES wants the float precision stated).

#include <string>

inline std::string glsl(const char* src) {
    std::string s = src ? src : "";
#ifdef __ANDROID__
    if (const size_t at = s.find("#version 330"); at != std::string::npos)
        s.replace(at, 12, "#version 300 es\nprecision highp float;\nprecision highp int;");
#endif
    return s;
}
