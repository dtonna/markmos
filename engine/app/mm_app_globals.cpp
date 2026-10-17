// Globals that outlive any one host.
//
// They used to live in mm_app_mac.mm / mm_app_ios.mm / mm_app_android.cpp. That
// works right up until a headless test links the engine and TOUCHES one of them:
// those three files carry the application's main(), so referencing a global declared
// next to it drags the whole host translation unit into the link and the test dies
// on a duplicate symbol. It is not obvious from the reference, which is why
// save_settings_tests broke on a change four files away from it.
//
// A definition with no main in it can be pulled in by anything. The hosts assign
// these; they do not have to own them.
#include "mm_app.hpp"

f32 g_content_scale = 1.0f;
f32 g_ui_inset_top   = 0.0f;
f32 g_ui_inset_bottom = 0.0f;
