#include "kerosene.h"

bool win32_register_window_class(HINSTANCE instance, const wchar_t *class_name, WNDPROC proc) {
    WNDCLASSEXW wc;
    memset(&wc, 0, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
    wc.lpfnWndProc = proc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    wc.hIconSm = wc.hIcon;
    wc.lpszClassName = class_name;
    return RegisterClassExW(&wc) || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
}
