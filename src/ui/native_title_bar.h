#pragma once

#include <QDockWidget>
#include <QTimer>

#ifdef Q_OS_WIN
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace ui::native_title_bar {

#ifdef Q_OS_WIN

    using dwm_set_window_attribute_fn = HRESULT(WINAPI*)(HWND, DWORD, LPCVOID, DWORD);

    inline dwm_set_window_attribute_fn get_dwm_set_window_attribute() {
        static const auto fn = []() -> dwm_set_window_attribute_fn {
            static const HMODULE dwmapi = LoadLibraryW(L"dwmapi.dll");
            if (!dwmapi) {
                return nullptr;
            }
            return reinterpret_cast<dwm_set_window_attribute_fn>(
                GetProcAddress(dwmapi, "DwmSetWindowAttribute"));
            }();
        return fn;
    }

    inline void apply_dark(QWidget* widget) {
        const auto set_window_attribute = get_dwm_set_window_attribute();
        if (!set_window_attribute) {
            return;
        }

        const HWND hwnd = reinterpret_cast<HWND>(widget->winId());
        const BOOL dark = TRUE;

        // DWMWA_USE_IMMERSIVE_DARK_MODE is 20 on current Windows SDKs.
        // Windows 10 1809 used the undocumented value 19, so fall back to it.
        constexpr DWORD immersive_dark_mode = 20;
        constexpr DWORD immersive_dark_mode_1809 = 19;

        const HRESULT result = set_window_attribute(
            hwnd, immersive_dark_mode, &dark, sizeof(dark));
        if (FAILED(result)) {
            set_window_attribute(
                hwnd, immersive_dark_mode_1809, &dark, sizeof(dark));
        }
    }

    inline void install(QDockWidget* dock) {
        // Create the native window handle and mark it dark before the user ever
        // starts an undock drag.  QDockWidget normally keeps this handle when it
        // transitions from docked child to floating top-level window, avoiding a
        // first-frame white native caption.
        dock->winId();
        apply_dark(dock);

        QObject::connect(dock, &QDockWidget::topLevelChanged, dock,
            [dock](bool floating) {
                if (!floating) {
                    return;
                }

                // Apply immediately so the native caption is dark during the
                // undocking transition itself.
                apply_dark(dock);

                // Reapply once Qt has finished the top-level transition as a
                // fallback in case Windows/Qt refreshed native-window state.
                QTimer::singleShot(0, dock, [dock]() {
                    apply_dark(dock);
                    });
            });
    }

#else

    inline void install(QDockWidget*) {}

#endif

} // namespace ui::native_title_bar