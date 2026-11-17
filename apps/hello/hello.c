/*
 * hello - template for a 4NSF app: one full-screen window, some text, tap to exit.
 *
 * The VW menu lists the app under "more" because of NgAppInfo.xml next to this file. When
 * the entry is tapped, the menu looks for a window with the class and title named there
 * (CLASS_NAME and TITLE below must match) and brings it to the front; if there is none it
 * starts the program with the arguments  language=eng-GBR skin=VW  (the menu's own settings).
 */
#include <windows.h>

#define CLASS_NAME L"Hello4NSF"
#define TITLE      L"Hello4NSF"

static const wchar_t *arguments = L"";

static LRESULT CALLBACK window_procedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
    switch (message) {
    case WM_PAINT: {
        PAINTSTRUCT paint;
        RECT top, bottom;
        HDC screen = BeginPaint(window, &paint);

        GetClientRect(window, &top);
        bottom = top;
        top.bottom = bottom.top = top.bottom / 2;
        DrawTextW(screen, L"hello 4NSF - tap to exit", -1, &top, DT_CENTER | DT_BOTTOM | DT_SINGLELINE);
        DrawTextW(screen, arguments, -1, &bottom, DT_CENTER | DT_TOP | DT_SINGLELINE);
        EndPaint(window, &paint);
        return 0;
    }
    case WM_LBUTTONUP:              /* the touch screen sends mouse messages */
        DestroyWindow(window);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

int WINAPI WinMain(HINSTANCE instance, HINSTANCE previous, LPWSTR command_line, int show)
{
    WNDCLASSW window_class;
    MSG message;

    arguments = command_line;
    memset(&window_class, 0, sizeof window_class);
    window_class.lpfnWndProc = window_procedure;
    window_class.hInstance = instance;
    window_class.hbrBackground = (HBRUSH)GetStockObject(WHITE_BRUSH);
    window_class.lpszClassName = CLASS_NAME;
    RegisterClassW(&window_class);

    CreateWindowExW(WS_EX_TOPMOST, CLASS_NAME, TITLE, WS_POPUP | WS_VISIBLE,
                    0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN),
                    NULL, NULL, instance, NULL);

    while (GetMessageW(&message, NULL, 0, 0)) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return (int)message.wParam;
}
