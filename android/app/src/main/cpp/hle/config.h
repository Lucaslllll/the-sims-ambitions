// Runtime configuration shared by the HLE layer, filled by the platform layer.
#pragma once

#include <string>

namespace hle {

struct Config {
    std::string data_root;     // external files dir
    std::string bundle_path;   // <data_root>/sims3dp_iphone.app
    std::string home_path;     // fake app sandbox root (Documents/Library/tmp)
    std::string language = "en";  // two-letter language code
    std::string region = "US";
    std::string device_model = "iPhone7,2";  // hw.machine reported to the game
    std::string system_version = "8.4";
    std::string gl_api = "OpenGLES1";  // value of Info.plist EAGLRenderingAPI
    // Logical screen in points, portrait (width < height), and scale.
    int screen_w = 375;
    int screen_h = 667;
    double screen_scale = 1.0;
    // Android window size in pixels (landscape).
    int window_w = 0;
    int window_h = 0;
};

Config& config();

}  // namespace hle
