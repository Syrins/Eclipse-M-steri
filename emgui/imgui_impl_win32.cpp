// dear MsTrG: Platform Backend for Windows (standard windows API for 32 and 64 bits applications)
// This needs to be used along with a Renderer (e.g. DirectX11, OpenGL3, Vulkan..)

// Implemented features:
//  [X] Platform: Clipboard support (for Win32 this is actually part of core dear MsTrG)
//  [X] Platform: Keyboard support. Since 1.87 we are using the io.AddKeyEvent() function. Pass MsTrGKey values to all key functions e.g. MsTrG::IsKeyPressed(MsTrGKey_Space). [Legacy VK_* values will also be supported unless MsTrG_DISABLE_OBSOLETE_KEYIO is set]
//  [X] Platform: Gamepad support. Enabled with 'io.ConfigFlags |= MsTrGConfigFlags_NavEnableGamepad'.
//  [X] Platform: Mouse cursor shape and visibility. Disable with 'io.ConfigFlags |= MsTrGConfigFlags_NoMouseCursorChange'.

// You can use unmodified MsTrG_impl_* files in your project. See examples/ folder for examples of using this.
// Prefer including the entire MsTrG/ repository into your project (either as a copy or as a submodule), and only build the backends you need.
// If you are new to Dear MsTrG, read documentation from the docs/ folder + read the top of MsTrG.cpp.
// Read online: https://github.com/ocornut/MsTrG/tree/master/docs

#include "imgui.h"
#include "imgui_impl_win32.h"
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <windowsx.h> // GET_X_LPARAM(), GET_Y_LPARAM()
#include <tchar.h>
#include <dwmapi.h>

// Configuration flags to add in your imconfig.h file:
//#define MsTrG_IMPL_WIN32_DISABLE_GAMEPAD              // Disable gamepad support. This was meaningful before <1.81 but we now load XInput dynamically so the option is now less relevant.

// Using XInput for gamepad (will load DLL dynamically)
#ifndef MsTrG_IMPL_WIN32_DISABLE_GAMEPAD
#include <xinput.h>
typedef DWORD(WINAPI* PFN_XInputGetCapabilities)(DWORD, DWORD, XINPUT_CAPABILITIES*);
typedef DWORD(WINAPI* PFN_XInputGetState)(DWORD, XINPUT_STATE*);
#endif
#include "../skCrypt.h"

// CHANGELOG
// (minor and older changes stripped away, please see git history for details)
//  2022-01-26: Inputs: replaced short-lived io.AddKeyModsEvent() (added two weeks ago)with io.AddKeyEvent() using MsTrGKey_ModXXX flags. Sorry for the confusion.
//  2021-01-20: Inputs: calling new io.AddKeyAnalogEvent() for gamepad support, instead of writing directly to io.NavInputs[].
//  2022-01-17: Inputs: calling new io.AddMousePosEvent(), io.AddMouseButtonEvent(), io.AddMouseWheelEvent() API (1.87+).
//  2022-01-17: Inputs: always update key mods next and before a key event (not in NewFrame) to fix input queue with very low framerates.
//  2022-01-12: Inputs: Update mouse inputs using WM_MOUSEMOVE/WM_MOUSELEAVE + fallback to provide it when focused but not hovered/captured. More standard and will allow us to pass it to future input queue API.
//  2022-01-12: Inputs: Maintain our own copy of MouseButtonsDown mask instead of using MsTrG::IsAnyMouseDown() which will be obsoleted.
//  2022-01-10: Inputs: calling new io.AddKeyEvent(), io.AddKeyModsEvent() + io.SetKeyEventNativeData() API (1.87+). Support for full MsTrGKey range.
//  2021-12-16: Inputs: Fill VK_LCONTROL/VK_RCONTROL/VK_LSHIFT/VK_RSHIFT/VK_LMENU/VK_RMENU for completeness.
//  2021-08-17: Calling io.AddFocusEvent() on WM_SETFOCUS/WM_KILLFOCUS messages.
//  2021-08-02: Inputs: Fixed keyboard modifiers being reported when host window doesn't have focus.
//  2021-07-29: Inputs: MousePos is correctly reported when the host platform window is hovered but not focused (using TrackMouseEvent() to receive WM_MOUSELEAVE events).
//  2021-06-29: Reorganized backend to pull data from a single structure to facilitate usage with multiple-contexts (all g_XXXX access changed to bd->XXXX).
//  2021-06-08: Fixed MsTrG_ImplWin32_EnableDpiAwareness() and MsTrG_ImplWin32_GetDpiScaleForMonitor() to handle Windows 8.1/10 features without a manifest (per-monitor DPI, and properly calls SetProcessDpiAwareness() on 8.1).
//  2021-03-23: Inputs: Clearing keyboard down array when losing focus (WM_KILLFOCUS).
//  2021-02-18: Added MsTrG_ImplWin32_EnableAlphaCompositing(). Non Visual Studio users will need to link with dwmapi.lib (MinGW/gcc: use -ldwmapi).
//  2021-02-17: Fixed MsTrG_ImplWin32_EnableDpiAwareness() attempting to get SetProcessDpiAwareness from shcore.dll on Windows 8 whereas it is only supported on Windows 8.1.
//  2021-01-25: Inputs: Dynamically loading XInput DLL.
//  2020-12-04: Misc: Fixed setting of io.DisplaySize to invalid/uninitialized data when after hwnd has been closed.
//  2020-03-03: Inputs: Calling AddInputCharacterUTF16() to support surrogate pairs leading to codepoint >= 0x10000 (for more complete CJK inputs)
//  2020-02-17: Added MsTrG_ImplWin32_EnableDpiAwareness(), MsTrG_ImplWin32_GetDpiScaleForHwnd(), MsTrG_ImplWin32_GetDpiScaleForMonitor() helper functions.
//  2020-01-14: Inputs: Added support for #define MsTrG_IMPL_WIN32_DISABLE_GAMEPAD/MsTrG_IMPL_WIN32_DISABLE_LINKING_XINPUT.
//  2019-12-05: Inputs: Added support for MsTrGMouseCursor_NotAllowed mouse cursor.
//  2019-05-11: Inputs: Don't filter value from WM_CHAR before calling AddInputCharacter().
//  2019-01-17: Misc: Using GetForegroundWindow()+IsChild() instead of GetActiveWindow() to be compatible with windows created in a different thread or parent.
//  2019-01-17: Inputs: Added support for mouse buttons 4 and 5 via WM_XBUTTON* messages.
//  2019-01-15: Inputs: Added support for XInput gamepads (if MsTrGConfigFlags_NavEnableGamepad is set by user application).
//  2018-11-30: Misc: Setting up io.BackendPlatformName so it can be displayed in the About Window.
//  2018-06-29: Inputs: Added support for the MsTrGMouseCursor_Hand cursor.
//  2018-06-10: Inputs: Fixed handling of mouse wheel messages to support fine position messages (typically sent by track-pads).
//  2018-06-08: Misc: Extracted MsTrG_impl_win32.cpp/.h away from the old combined DX9/DX10/DX11/DX12 examples.
//  2018-03-20: Misc: Setup io.BackendFlags MsTrGBackendFlags_HasMouseCursors and MsTrGBackendFlags_HasSetMousePos flags + honor MsTrGConfigFlags_NoMouseCursorChange flag.
//  2018-02-20: Inputs: Added support for mouse cursors (MsTrG::GetMouseCursor() value and WM_SETCURSOR message handling).
//  2018-02-06: Inputs: Added mapping for MsTrGKey_Space.
//  2018-02-06: Inputs: Honoring the io.WantSetMousePos by repositioning the mouse (when using navigation and MsTrGConfigFlags_NavMoveMouse is set).
//  2018-02-06: Misc: Removed call to MsTrG::Shutdown() which is not available from 1.60 WIP, user needs to call CreateContext/DestroyContext themselves.
//  2018-01-20: Inputs: Added Horizontal Mouse Wheel support.
//  2018-01-08: Inputs: Added mapping for MsTrGKey_Insert.
//  2018-01-05: Inputs: Added WM_LBUTTONDBLCLK double-click handlers for window classes with the CS_DBLCLKS flag.
//  2017-10-23: Inputs: Added WM_SYSKEYDOWN / WM_SYSKEYUP handlers so e.g. the VK_MENU key can be read.
//  2017-10-23: Inputs: Using Win32 ::SetCapture/::GetCapture() to retrieve mouse positions outside the client area when dragging.
//  2016-11-12: Inputs: Only call Win32 ::SetCursor(NULL) when io.MouseDrawCursor is set.

struct MsTrG_ImplWin32_Data
{
    HWND                        hWnd;
    HWND                        MouseHwnd;
    bool                        MouseTracked;
    int                         MouseButtonsDown;
    INT64                       Time;
    INT64                       TicksPerSecond;
    MsTrGMouseCursor            LastMouseCursor;
    bool                        HasGamepad;
    bool                        WantUpdateHasGamepad;

#ifndef MsTrG_IMPL_WIN32_DISABLE_GAMEPAD
    HMODULE                     XInputDLL;
    PFN_XInputGetCapabilities   XInputGetCapabilities;
    PFN_XInputGetState          XInputGetState;
#endif

    MsTrG_ImplWin32_Data() { memset((void*)this, 0, sizeof(*this)); }
};

// Backend data stored in io.BackendPlatformUserData to allow support for multiple Dear MsTrG contexts
// It is STRONGLY preferred that you use docking branch with multi-viewports (== single Dear MsTrG context + multiple windows) instead of multiple Dear MsTrG contexts.
// FIXME: multi-context support is not well tested and probably dysfunctional in this backend.
// FIXME: some shared resources (mouse cursor shape, gamepad) are mishandled when using multi-context.
static MsTrG_ImplWin32_Data* MsTrG_ImplWin32_GetBackendData()
{
    return MsTrG::GetCurrentContext() ? (MsTrG_ImplWin32_Data*)MsTrG::GetIO().BackendPlatformUserData : NULL;
}

// Functions
bool    MsTrG_ImplWin32_Init(void* hwnd)
{
    MsTrGIO& io = MsTrG::GetIO();
    IM_ASSERT(io.BackendPlatformUserData == NULL&&);

    INT64 perf_frequency, perf_counter;
    if (!::QueryPerformanceFrequency((LARGE_INTEGER*)&perf_frequency))
        return false;
    if (!::QueryPerformanceCounter((LARGE_INTEGER*)&perf_counter))
        return false;

    // Setup backend capabilities flags
    MsTrG_ImplWin32_Data* bd = IM_NEW(MsTrG_ImplWin32_Data)();
    io.BackendPlatformUserData = (void*)bd;
    io.BackendPlatformName = skCrypt("eZ");
    io.BackendFlags |= MsTrGBackendFlags_HasMouseCursors;         // We can honor GetMouseCursor() values (optional)
    io.BackendFlags |= MsTrGBackendFlags_HasSetMousePos;          // We can honor io.WantSetMousePos requests (optional, rarely used)

    bd->hWnd = (HWND)hwnd;
    bd->WantUpdateHasGamepad = true;
    bd->TicksPerSecond = perf_frequency;
    bd->Time = perf_counter;
    bd->LastMouseCursor = MsTrGMouseCursor_COUNT;

    // Set platform dependent data in viewport
    MsTrG::GetMainViewport()->PlatformHandleRaw = (void*)hwnd;

    // Dynamically load XInput library
#ifndef MsTrG_IMPL_WIN32_DISABLE_GAMEPAD
    const char* xinput_dll_names[] =
    {
        "xinput1_4.dll",   // Windows 8+
        "xinput1_3.dll",   // DirectX SDK
        "xinput9_1_0.dll", // Windows Vista, Windows 7
        "xinput1_2.dll",   // DirectX SDK
        "xinput1_1.dll"    // DirectX SDK
    };
    for (int n = 0; n < IM_ARRAYSIZE(xinput_dll_names); n++)
        if (HMODULE dll = ::LoadLibraryA(xinput_dll_names[n]))
        {
            bd->XInputDLL = dll;
            bd->XInputGetCapabilities = (PFN_XInputGetCapabilities)::GetProcAddress(dll, "XInputGetCapabilities");
            bd->XInputGetState = (PFN_XInputGetState)::GetProcAddress(dll, "XInputGetState");
            break;
        }
#endif // MsTrG_IMPL_WIN32_DISABLE_GAMEPAD

    return true;
}

void    MsTrG_ImplWin32_Shutdown()
{
    MsTrG_ImplWin32_Data* bd = MsTrG_ImplWin32_GetBackendData();
    IM_ASSERT(bd != NULL&&);
    MsTrGIO& io = MsTrG::GetIO();

    // Unload XInput library
#ifndef MsTrG_IMPL_WIN32_DISABLE_GAMEPAD
    if (bd->XInputDLL)
        ::FreeLibrary(bd->XInputDLL);
#endif // MsTrG_IMPL_WIN32_DISABLE_GAMEPAD

    io.BackendPlatformName = NULL;
    io.BackendPlatformUserData = NULL;
    IM_DELETE(bd);
}

static bool MsTrG_ImplWin32_UpdateMouseCursor()
{
    MsTrGIO& io = MsTrG::GetIO();
    if (io.ConfigFlags & MsTrGConfigFlags_NoMouseCursorChange)
        return false;

    MsTrGMouseCursor MsTrG_cursor = MsTrG::GetMouseCursor();
    if (MsTrG_cursor == MsTrGMouseCursor_None || io.MouseDrawCursor)
    {
        // Hide OS mouse cursor if MsTrG is drawing it or if it wants no cursor
        ::SetCursor(NULL);
    }
    else
    {
        // Show OS mouse cursor
        LPTSTR win32_cursor = IDC_ARROW;
        switch (MsTrG_cursor)
        {
        case MsTrGMouseCursor_Arrow:        win32_cursor = IDC_ARROW; break;
        case MsTrGMouseCursor_TextInput:    win32_cursor = IDC_IBEAM; break;
        case MsTrGMouseCursor_ResizeAll:    win32_cursor = IDC_SIZEALL; break;
        case MsTrGMouseCursor_ResizeEW:     win32_cursor = IDC_SIZEWE; break;
        case MsTrGMouseCursor_ResizeNS:     win32_cursor = IDC_SIZENS; break;
        case MsTrGMouseCursor_ResizeNESW:   win32_cursor = IDC_SIZENESW; break;
        case MsTrGMouseCursor_ResizeNWSE:   win32_cursor = IDC_SIZENWSE; break;
        case MsTrGMouseCursor_Hand:         win32_cursor = IDC_HAND; break;
        case MsTrGMouseCursor_NotAllowed:   win32_cursor = IDC_NO; break;
        }
        ::SetCursor(::LoadCursor(NULL, win32_cursor));
    }
    return true;
}

static bool IsVkDown(int vk)
{
    return (::GetKeyState(vk) & 0x8000) != 0;
}

static void MsTrG_ImplWin32_AddKeyEvent(MsTrGKey key, bool down, int native_keycode, int native_scancode = -1)
{
    MsTrGIO& io = MsTrG::GetIO();
    io.AddKeyEvent(key, down);
    io.SetKeyEventNativeData(key, native_keycode, native_scancode); // To support legacy indexing (<1.87 user code)
    IM_UNUSED(native_scancode);
}

static void MsTrG_ImplWin32_ProcessKeyEventsWorkarounds()
{
    // Left & right Shift keys: when both are pressed together, Windows tend to not generate the WM_KEYUP event for the first released one.
    if (MsTrG::IsKeyDown(MsTrGKey_LeftShift) && !IsVkDown(VK_LSHIFT))
        MsTrG_ImplWin32_AddKeyEvent(MsTrGKey_LeftShift, false, VK_LSHIFT);
    if (MsTrG::IsKeyDown(MsTrGKey_RightShift) && !IsVkDown(VK_RSHIFT))
        MsTrG_ImplWin32_AddKeyEvent(MsTrGKey_RightShift, false, VK_RSHIFT);

    // Sometimes WM_KEYUP for Win key is not passed down to the app (e.g. for Win+V on some setups, according to GLFW).
    if (MsTrG::IsKeyDown(MsTrGKey_LeftSuper) && !IsVkDown(VK_LWIN))
        MsTrG_ImplWin32_AddKeyEvent(MsTrGKey_LeftSuper, false, VK_LWIN);
    if (MsTrG::IsKeyDown(MsTrGKey_RightSuper) && !IsVkDown(VK_RWIN))
        MsTrG_ImplWin32_AddKeyEvent(MsTrGKey_RightSuper, false, VK_RWIN);
}

static void MsTrG_ImplWin32_UpdateKeyModifiers()
{
    MsTrGIO& io = MsTrG::GetIO();
    io.AddKeyEvent(MsTrGKey_ModCtrl, IsVkDown(VK_CONTROL));
    io.AddKeyEvent(MsTrGKey_ModShift, IsVkDown(VK_SHIFT));
    io.AddKeyEvent(MsTrGKey_ModAlt, IsVkDown(VK_MENU));
    io.AddKeyEvent(MsTrGKey_ModSuper, IsVkDown(VK_APPS));
}

static void MsTrG_ImplWin32_UpdateMouseData()
{
    MsTrG_ImplWin32_Data* bd = MsTrG_ImplWin32_GetBackendData();
    MsTrGIO& io = MsTrG::GetIO();
    IM_ASSERT(bd->hWnd != 0);

    const bool is_app_focused = (::GetForegroundWindow() == bd->hWnd);
    if (is_app_focused)
    {
        // (Optional) Set OS mouse position from Dear MsTrG if requested (rarely used, only when MsTrGConfigFlags_NavEnableSetMousePos is enabled by user)
        if (io.WantSetMousePos)
        {
            POINT pos = { (int)io.MousePos.x, (int)io.MousePos.y };
            if (::ClientToScreen(bd->hWnd, &pos))
                ::SetCursorPos(pos.x, pos.y);
        }

        // (Optional) Fallback to provide mouse position when focused (WM_MOUSEMOVE already provides this when hovered or captured)
        if (!io.WantSetMousePos && !bd->MouseTracked)
        {
            POINT pos;
            if (::GetCursorPos(&pos) && ::ScreenToClient(bd->hWnd, &pos))
                io.AddMousePosEvent((float)pos.x, (float)pos.y);
        }
    }
}

// Gamepad navigation mapping
static void MsTrG_ImplWin32_UpdateGamepads()
{
#ifndef MsTrG_IMPL_WIN32_DISABLE_GAMEPAD
    MsTrGIO& io = MsTrG::GetIO();
    MsTrG_ImplWin32_Data* bd = MsTrG_ImplWin32_GetBackendData();
    if ((io.ConfigFlags & MsTrGConfigFlags_NavEnableGamepad) == 0)
        return;

    // Calling XInputGetState() every frame on disconnected gamepads is unfortunately too slow.
    // Instead we refresh gamepad availability by calling XInputGetCapabilities() _only_ after receiving WM_DEVICECHANGE.
    if (bd->WantUpdateHasGamepad)
    {
        XINPUT_CAPABILITIES caps = {};
        bd->HasGamepad = bd->XInputGetCapabilities ? (bd->XInputGetCapabilities(0, XINPUT_FLAG_GAMEPAD, &caps) == ERROR_SUCCESS) : false;
        bd->WantUpdateHasGamepad = false;
    }

    io.BackendFlags &= ~MsTrGBackendFlags_HasGamepad;
    XINPUT_STATE xinput_state;
    XINPUT_GAMEPAD& gamepad = xinput_state.Gamepad;
    if (!bd->HasGamepad || bd->XInputGetState == NULL || bd->XInputGetState(0, &xinput_state) != ERROR_SUCCESS)
        return;
    io.BackendFlags |= MsTrGBackendFlags_HasGamepad;

#define IM_SATURATE(V)                      (V < 0.0f ? 0.0f : V > 1.0f ? 1.0f : V)
#define MAP_BUTTON(KEY_NO, BUTTON_ENUM)     { io.AddKeyEvent(KEY_NO, (gamepad.wButtons & BUTTON_ENUM) != 0); }
#define MAP_ANALOG(KEY_NO, VALUE, V0, V1)   { float vn = (float)(VALUE - V0) / (float)(V1 - V0); io.AddKeyAnalogEvent(KEY_NO, vn > 0.10f, IM_SATURATE(vn)); }
    MAP_BUTTON(MsTrGKey_GamepadStart, XINPUT_GAMEPAD_START);
    MAP_BUTTON(MsTrGKey_GamepadBack, XINPUT_GAMEPAD_BACK);
    MAP_BUTTON(MsTrGKey_GamepadFaceDown, XINPUT_GAMEPAD_A);
    MAP_BUTTON(MsTrGKey_GamepadFaceRight, XINPUT_GAMEPAD_B);
    MAP_BUTTON(MsTrGKey_GamepadFaceLeft, XINPUT_GAMEPAD_X);
    MAP_BUTTON(MsTrGKey_GamepadFaceUp, XINPUT_GAMEPAD_Y);
    MAP_BUTTON(MsTrGKey_GamepadDpadLeft, XINPUT_GAMEPAD_DPAD_LEFT);
    MAP_BUTTON(MsTrGKey_GamepadDpadRight, XINPUT_GAMEPAD_DPAD_RIGHT);
    MAP_BUTTON(MsTrGKey_GamepadDpadUp, XINPUT_GAMEPAD_DPAD_UP);
    MAP_BUTTON(MsTrGKey_GamepadDpadDown, XINPUT_GAMEPAD_DPAD_DOWN);
    MAP_BUTTON(MsTrGKey_GamepadL1, XINPUT_GAMEPAD_LEFT_SHOULDER);
    MAP_BUTTON(MsTrGKey_GamepadR1, XINPUT_GAMEPAD_RIGHT_SHOULDER);
    MAP_ANALOG(MsTrGKey_GamepadL2, gamepad.bLeftTrigger, XINPUT_GAMEPAD_TRIGGER_THRESHOLD, 255);
    MAP_ANALOG(MsTrGKey_GamepadR2, gamepad.bRightTrigger, XINPUT_GAMEPAD_TRIGGER_THRESHOLD, 255);
    MAP_BUTTON(MsTrGKey_GamepadL3, XINPUT_GAMEPAD_LEFT_THUMB);
    MAP_BUTTON(MsTrGKey_GamepadR3, XINPUT_GAMEPAD_RIGHT_THUMB);
    MAP_ANALOG(MsTrGKey_GamepadLStickLeft, gamepad.sThumbLX, -XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE, -32768);
    MAP_ANALOG(MsTrGKey_GamepadLStickRight, gamepad.sThumbLX, +XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE, +32767);
    MAP_ANALOG(MsTrGKey_GamepadLStickUp, gamepad.sThumbLY, +XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE, +32767);
    MAP_ANALOG(MsTrGKey_GamepadLStickDown, gamepad.sThumbLY, -XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE, -32768);
    MAP_ANALOG(MsTrGKey_GamepadRStickLeft, gamepad.sThumbRX, -XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE, -32768);
    MAP_ANALOG(MsTrGKey_GamepadRStickRight, gamepad.sThumbRX, +XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE, +32767);
    MAP_ANALOG(MsTrGKey_GamepadRStickUp, gamepad.sThumbRY, +XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE, +32767);
    MAP_ANALOG(MsTrGKey_GamepadRStickDown, gamepad.sThumbRY, -XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE, -32768);
#undef MAP_BUTTON
#undef MAP_ANALOG
#endif // #ifndef MsTrG_IMPL_WIN32_DISABLE_GAMEPAD
}

void    MsTrG_ImplWin32_NewFrame()
{
    MsTrGIO& io = MsTrG::GetIO();
    MsTrG_ImplWin32_Data* bd = MsTrG_ImplWin32_GetBackendData();
    IM_ASSERT(bd != NULL&&);

    // Setup display size (every frame to accommodate for window resizing)
    RECT rect = { 0, 0, 0, 0 };
    ::GetClientRect(bd->hWnd, &rect);
    io.DisplaySize = ImVec2((float)(rect.right - rect.left), (float)(rect.bottom - rect.top));

    // Setup time step
    INT64 current_time = 0;
    ::QueryPerformanceCounter((LARGE_INTEGER*)&current_time);
    io.DeltaTime = (float)(current_time - bd->Time) / bd->TicksPerSecond;
    bd->Time = current_time;

    // Update OS mouse position
    MsTrG_ImplWin32_UpdateMouseData();

    // Process workarounds for known Windows key handling issues
    MsTrG_ImplWin32_ProcessKeyEventsWorkarounds();

    // Update OS mouse cursor with the cursor requested by MsTrG
    MsTrGMouseCursor mouse_cursor = io.MouseDrawCursor ? MsTrGMouseCursor_None : MsTrG::GetMouseCursor();
    if (bd->LastMouseCursor != mouse_cursor)
    {
        bd->LastMouseCursor = mouse_cursor;
        MsTrG_ImplWin32_UpdateMouseCursor();
    }

    // Update game controllers (if enabled and available)
    MsTrG_ImplWin32_UpdateGamepads();
}

// There is no distinct VK_xxx for keypad enter, instead it is VK_RETURN + KF_EXTENDED, we assign it an arbitrary value to make code more readable (VK_ codes go up to 255)
#define IM_VK_KEYPAD_ENTER      (VK_RETURN + 256)

// Map VK_xxx to MsTrGKey_xxx.
static MsTrGKey MsTrG_ImplWin32_VirtualKeyToMsTrGKey(WPARAM wParam)
{
    switch (wParam)
    {
    case VK_TAB: return MsTrGKey_Tab;
    case VK_LEFT: return MsTrGKey_LeftArrow;
    case VK_RIGHT: return MsTrGKey_RightArrow;
    case VK_UP: return MsTrGKey_UpArrow;
    case VK_DOWN: return MsTrGKey_DownArrow;
    case VK_PRIOR: return MsTrGKey_PageUp;
    case VK_NEXT: return MsTrGKey_PageDown;
    case VK_HOME: return MsTrGKey_Home;
    case VK_END: return MsTrGKey_End;
    case VK_INSERT: return MsTrGKey_Insert;
    case VK_DELETE: return MsTrGKey_Delete;
    case VK_BACK: return MsTrGKey_Backspace;
    case VK_SPACE: return MsTrGKey_Space;
    case VK_RETURN: return MsTrGKey_Enter;
    case VK_ESCAPE: return MsTrGKey_Escape;
    case VK_OEM_7: return MsTrGKey_Apostrophe;
    case VK_OEM_COMMA: return MsTrGKey_Comma;
    case VK_OEM_MINUS: return MsTrGKey_Minus;
    case VK_OEM_PERIOD: return MsTrGKey_Period;
    case VK_OEM_2: return MsTrGKey_Slash;
    case VK_OEM_1: return MsTrGKey_Semicolon;
    case VK_OEM_PLUS: return MsTrGKey_Equal;
    case VK_OEM_4: return MsTrGKey_LeftBracket;
    case VK_OEM_5: return MsTrGKey_Backslash;
    case VK_OEM_6: return MsTrGKey_RightBracket;
    case VK_OEM_3: return MsTrGKey_GraveAccent;
    case VK_CAPITAL: return MsTrGKey_CapsLock;
    case VK_SCROLL: return MsTrGKey_ScrollLock;
    case VK_NUMLOCK: return MsTrGKey_NumLock;
    case VK_SNAPSHOT: return MsTrGKey_PrintScreen;
    case VK_PAUSE: return MsTrGKey_Pause;
    case VK_NUMPAD0: return MsTrGKey_Keypad0;
    case VK_NUMPAD1: return MsTrGKey_Keypad1;
    case VK_NUMPAD2: return MsTrGKey_Keypad2;
    case VK_NUMPAD3: return MsTrGKey_Keypad3;
    case VK_NUMPAD4: return MsTrGKey_Keypad4;
    case VK_NUMPAD5: return MsTrGKey_Keypad5;
    case VK_NUMPAD6: return MsTrGKey_Keypad6;
    case VK_NUMPAD7: return MsTrGKey_Keypad7;
    case VK_NUMPAD8: return MsTrGKey_Keypad8;
    case VK_NUMPAD9: return MsTrGKey_Keypad9;
    case VK_DECIMAL: return MsTrGKey_KeypadDecimal;
    case VK_DIVIDE: return MsTrGKey_KeypadDivide;
    case VK_MULTIPLY: return MsTrGKey_KeypadMultiply;
    case VK_SUBTRACT: return MsTrGKey_KeypadSubtract;
    case VK_ADD: return MsTrGKey_KeypadAdd;
    case IM_VK_KEYPAD_ENTER: return MsTrGKey_KeypadEnter;
    case VK_LSHIFT: return MsTrGKey_LeftShift;
    case VK_LCONTROL: return MsTrGKey_LeftCtrl;
    case VK_LMENU: return MsTrGKey_LeftAlt;
    case VK_LWIN: return MsTrGKey_LeftSuper;
    case VK_RSHIFT: return MsTrGKey_RightShift;
    case VK_RCONTROL: return MsTrGKey_RightCtrl;
    case VK_RMENU: return MsTrGKey_RightAlt;
    case VK_RWIN: return MsTrGKey_RightSuper;
    case VK_APPS: return MsTrGKey_Menu;
    case '0': return MsTrGKey_0;
    case '1': return MsTrGKey_1;
    case '2': return MsTrGKey_2;
    case '3': return MsTrGKey_3;
    case '4': return MsTrGKey_4;
    case '5': return MsTrGKey_5;
    case '6': return MsTrGKey_6;
    case '7': return MsTrGKey_7;
    case '8': return MsTrGKey_8;
    case '9': return MsTrGKey_9;
    case 'A': return MsTrGKey_A;
    case 'B': return MsTrGKey_B;
    case 'C': return MsTrGKey_C;
    case 'D': return MsTrGKey_D;
    case 'E': return MsTrGKey_E;
    case 'F': return MsTrGKey_F;
    case 'G': return MsTrGKey_G;
    case 'H': return MsTrGKey_H;
    case 'I': return MsTrGKey_I;
    case 'J': return MsTrGKey_J;
    case 'K': return MsTrGKey_K;
    case 'L': return MsTrGKey_L;
    case 'M': return MsTrGKey_M;
    case 'N': return MsTrGKey_N;
    case 'O': return MsTrGKey_O;
    case 'P': return MsTrGKey_P;
    case 'Q': return MsTrGKey_Q;
    case 'R': return MsTrGKey_R;
    case 'S': return MsTrGKey_S;
    case 'T': return MsTrGKey_T;
    case 'U': return MsTrGKey_U;
    case 'V': return MsTrGKey_V;
    case 'W': return MsTrGKey_W;
    case 'X': return MsTrGKey_X;
    case 'Y': return MsTrGKey_Y;
    case 'Z': return MsTrGKey_Z;
    case VK_F1: return MsTrGKey_F1;
    case VK_F2: return MsTrGKey_F2;
    case VK_F3: return MsTrGKey_F3;
    case VK_F4: return MsTrGKey_F4;
    case VK_F5: return MsTrGKey_F5;
    case VK_F6: return MsTrGKey_F6;
    case VK_F7: return MsTrGKey_F7;
    case VK_F8: return MsTrGKey_F8;
    case VK_F9: return MsTrGKey_F9;
    case VK_F10: return MsTrGKey_F10;
    case VK_F11: return MsTrGKey_F11;
    case VK_F12: return MsTrGKey_F12;
    default: return MsTrGKey_None;
    }
}

// Allow compilation with old Windows SDK. MinGW doesn't have default _WIN32_WINNT/WINVER versions.
#ifndef WM_MOUSEHWHEEL
#define WM_MOUSEHWHEEL 0x020E
#endif
#ifndef DBT_DEVNODES_CHANGED
#define DBT_DEVNODES_CHANGED 0x0007
#endif

// Win32 message handler (process Win32 mouse/keyboard inputs, etc.)
// Call from your application's message handler. Keep calling your message handler unless this function returns TRUE.
// When implementing your own backend, you can read the io.WantCaptureMouse, io.WantCaptureKeyboard flags to tell if Dear MsTrG wants to use your inputs.
// - When io.WantCaptureMouse is true, do not dispatch mouse input data to your main application, or clear/overwrite your copy of the mouse data.
// - When io.WantCaptureKeyboard is true, do not dispatch keyboard input data to your main application, or clear/overwrite your copy of the keyboard data.
// Generally you may always pass all inputs to Dear MsTrG, and hide them from your application based on those two flags.
// PS: In this Win32 handler, we use the capture API (GetCapture/SetCapture/ReleaseCapture) to be able to read mouse coordinates when dragging mouse outside of our window bounds.
// PS: We treat DBLCLK messages as regular mouse down messages, so this code will work on windows classes that have the CS_DBLCLKS flag set. Our own example app code doesn't set this flag.
#if 0
// Copy this line into your .cpp file to forward declare the function.
extern MsTrG_IMPL_API LRESULT MsTrG_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
#endif
MsTrG_IMPL_API LRESULT MsTrG_ImplWin32_WndProcHandler(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (MsTrG::GetCurrentContext() == NULL)
        return 0;

    MsTrGIO& io = MsTrG::GetIO();
    MsTrG_ImplWin32_Data* bd = MsTrG_ImplWin32_GetBackendData();

    switch (msg)
    {
    case WM_MOUSEMOVE:
        // We need to call TrackMouseEvent in order to receive WM_MOUSELEAVE events
        bd->MouseHwnd = hwnd;
        if (!bd->MouseTracked)
        {
            TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, hwnd, 0 };
            ::TrackMouseEvent(&tme);
            bd->MouseTracked = true;
        }
        io.AddMousePosEvent((float)GET_X_LPARAM(lParam), (float)GET_Y_LPARAM(lParam));
        break;
    case WM_MOUSELEAVE:
        if (bd->MouseHwnd == hwnd)
            bd->MouseHwnd = NULL;
        bd->MouseTracked = false;
        io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
        break;
    case WM_LBUTTONDOWN: case WM_LBUTTONDBLCLK:
    case WM_RBUTTONDOWN: case WM_RBUTTONDBLCLK:
    case WM_MBUTTONDOWN: case WM_MBUTTONDBLCLK:
    case WM_XBUTTONDOWN: case WM_XBUTTONDBLCLK:
    {
        int button = 0;
        if (msg == WM_LBUTTONDOWN || msg == WM_LBUTTONDBLCLK) { button = 0; }
        if (msg == WM_RBUTTONDOWN || msg == WM_RBUTTONDBLCLK) { button = 1; }
        if (msg == WM_MBUTTONDOWN || msg == WM_MBUTTONDBLCLK) { button = 2; }
        if (msg == WM_XBUTTONDOWN || msg == WM_XBUTTONDBLCLK) { button = (GET_XBUTTON_WPARAM(wParam) == XBUTTON1) ? 3 : 4; }
        if (bd->MouseButtonsDown == 0 && ::GetCapture() == NULL)
            ::SetCapture(hwnd);
        bd->MouseButtonsDown |= 1 << button;
        io.AddMouseButtonEvent(button, true);
        return 0;
    }
    case WM_LBUTTONUP:
    case WM_RBUTTONUP:
    case WM_MBUTTONUP:
    case WM_XBUTTONUP:
    {
        int button = 0;
        if (msg == WM_LBUTTONUP) { button = 0; }
        if (msg == WM_RBUTTONUP) { button = 1; }
        if (msg == WM_MBUTTONUP) { button = 2; }
        if (msg == WM_XBUTTONUP) { button = (GET_XBUTTON_WPARAM(wParam) == XBUTTON1) ? 3 : 4; }
        bd->MouseButtonsDown &= ~(1 << button);
        if (bd->MouseButtonsDown == 0 && ::GetCapture() == hwnd)
            ::ReleaseCapture();
        io.AddMouseButtonEvent(button, false);
        return 0;
    }
    case WM_MOUSEWHEEL:
        io.AddMouseWheelEvent(0.0f, (float)GET_WHEEL_DELTA_WPARAM(wParam) / (float)WHEEL_DELTA);
        return 0;
    case WM_MOUSEHWHEEL:
        io.AddMouseWheelEvent((float)GET_WHEEL_DELTA_WPARAM(wParam) / (float)WHEEL_DELTA, 0.0f);
        return 0;
    case WM_KEYDOWN:
    case WM_KEYUP:
    case WM_SYSKEYDOWN:
    case WM_SYSKEYUP:
    {
        const bool is_key_down = (msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN);
        if (wParam < 256)
        {
            // Submit modifiers
            MsTrG_ImplWin32_UpdateKeyModifiers();

            // Obtain virtual key code
            // (keypad enter doesn't have its own... VK_RETURN with KF_EXTENDED flag means keypad enter, see IM_VK_KEYPAD_ENTER definition for details, it is mapped to MsTrGKey_KeyPadEnter.)
            int vk = (int)wParam;
            if ((wParam == VK_RETURN) && (HIWORD(lParam) & KF_EXTENDED))
                vk = IM_VK_KEYPAD_ENTER;

            // Submit key event
            const MsTrGKey key = MsTrG_ImplWin32_VirtualKeyToMsTrGKey(vk);
            const int scancode = (int)LOBYTE(HIWORD(lParam));
            if (key != MsTrGKey_None)
                MsTrG_ImplWin32_AddKeyEvent(key, is_key_down, vk, scancode);

            // Submit individual left/right modifier events
            if (vk == VK_SHIFT)
            {
                // Important: Shift keys tend to get stuck when pressed together, missing key-up events are corrected in MsTrG_ImplWin32_ProcessKeyEventsWorkarounds()
                if (IsVkDown(VK_LSHIFT) == is_key_down) { MsTrG_ImplWin32_AddKeyEvent(MsTrGKey_LeftShift, is_key_down, VK_LSHIFT, scancode); }
                if (IsVkDown(VK_RSHIFT) == is_key_down) { MsTrG_ImplWin32_AddKeyEvent(MsTrGKey_RightShift, is_key_down, VK_RSHIFT, scancode); }
            }
            else if (vk == VK_CONTROL)
            {
                if (IsVkDown(VK_LCONTROL) == is_key_down) { MsTrG_ImplWin32_AddKeyEvent(MsTrGKey_LeftCtrl, is_key_down, VK_LCONTROL, scancode); }
                if (IsVkDown(VK_RCONTROL) == is_key_down) { MsTrG_ImplWin32_AddKeyEvent(MsTrGKey_RightCtrl, is_key_down, VK_RCONTROL, scancode); }
            }
            else if (vk == VK_MENU)
            {
                if (IsVkDown(VK_LMENU) == is_key_down) { MsTrG_ImplWin32_AddKeyEvent(MsTrGKey_LeftAlt, is_key_down, VK_LMENU, scancode); }
                if (IsVkDown(VK_RMENU) == is_key_down) { MsTrG_ImplWin32_AddKeyEvent(MsTrGKey_RightAlt, is_key_down, VK_RMENU, scancode); }
            }
        }
        return 0;
    }
    case WM_SETFOCUS:
    case WM_KILLFOCUS:
        io.AddFocusEvent(msg == WM_SETFOCUS);
        return 0;
    case WM_CHAR:
        // You can also use ToAscii()+GetKeyboardState() to retrieve characters.
        if (wParam > 0 && wParam < 0x10000)
            io.AddInputCharacterUTF16((unsigned short)wParam);
        return 0;
    case WM_SETCURSOR:
        // This is required to restore cursor when transitioning from e.g resize borders to client area.
        if (LOWORD(lParam) == HTCLIENT && MsTrG_ImplWin32_UpdateMouseCursor())
            return 1;
        return 0;
    case WM_DEVICECHANGE:
        if ((UINT)wParam == DBT_DEVNODES_CHANGED)
            bd->WantUpdateHasGamepad = true;
        return 0;
    }
    return 0;
}


//--------------------------------------------------------------------------------------------------------
// DPI-related helpers (optional)
//--------------------------------------------------------------------------------------------------------
// - Use to enable DPI awareness without having to create an application manifest.
// - Your own app may already do this via a manifest or explicit calls. This is mostly useful for our examples/ apps.
// - In theory we could call simple functions from Windows SDK such as SetProcessDPIAware(), SetProcessDpiAwareness(), etc.
//   but most of the functions provided by Microsoft require Windows 8.1/10+ SDK at compile time and Windows 8/10+ at runtime,
//   neither we want to require the user to have. So we dynamically select and load those functions to avoid dependencies.
//---------------------------------------------------------------------------------------------------------
// This is the scheme successfully used by GLFW (from which we borrowed some of the code) and other apps aiming to be highly portable.
// MsTrG_ImplWin32_EnableDpiAwareness() is just a helper called by main.cpp, we don't call it automatically.
// If you are trying to implement your own backend for your own engine, you may ignore that noise.
//---------------------------------------------------------------------------------------------------------

// Perform our own check with RtlVerifyVersionInfo() instead of using functions from <VersionHelpers.h> as they
// require a manifest to be functional for checks above 8.1. See https://github.com/ocornut/MsTrG/issues/4200
static BOOL _IsWindowsVersionOrGreater(WORD major, WORD minor, WORD)
{
    typedef LONG(WINAPI* PFN_RtlVerifyVersionInfo)(OSVERSIONINFOEXW*, ULONG, ULONGLONG);
    static PFN_RtlVerifyVersionInfo RtlVerifyVersionInfoFn = NULL;
    if (RtlVerifyVersionInfoFn == NULL)
        if (HMODULE ntdllModule = ::GetModuleHandleA("ntdll.dll"))
            RtlVerifyVersionInfoFn = (PFN_RtlVerifyVersionInfo)GetProcAddress(ntdllModule, "RtlVerifyVersionInfo");
    if (RtlVerifyVersionInfoFn == NULL)
        return FALSE;

    RTL_OSVERSIONINFOEXW versionInfo = { };
    ULONGLONG conditionMask = 0;
    versionInfo.dwOSVersionInfoSize = sizeof(RTL_OSVERSIONINFOEXW);
    versionInfo.dwMajorVersion = major;
    versionInfo.dwMinorVersion = minor;
    VER_SET_CONDITION(conditionMask, VER_MAJORVERSION, VER_GREATER_EQUAL);
    VER_SET_CONDITION(conditionMask, VER_MINORVERSION, VER_GREATER_EQUAL);
    return (RtlVerifyVersionInfoFn(&versionInfo, VER_MAJORVERSION | VER_MINORVERSION, conditionMask) == 0) ? TRUE : FALSE;
}

#define _IsWindowsVistaOrGreater()   _IsWindowsVersionOrGreater(HIBYTE(0x0600), LOBYTE(0x0600), 0) // _WIN32_WINNT_VISTA
#define _IsWindows8OrGreater()       _IsWindowsVersionOrGreater(HIBYTE(0x0602), LOBYTE(0x0602), 0) // _WIN32_WINNT_WIN8
#define _IsWindows8Point1OrGreater() _IsWindowsVersionOrGreater(HIBYTE(0x0603), LOBYTE(0x0603), 0) // _WIN32_WINNT_WINBLUE
#define _IsWindows10OrGreater()      _IsWindowsVersionOrGreater(HIBYTE(0x0A00), LOBYTE(0x0A00), 0) // _WIN32_WINNT_WINTHRESHOLD / _WIN32_WINNT_WIN10

#ifndef DPI_ENUMS_DECLARED
typedef enum { PROCESS_DPI_UNAWARE = 0, PROCESS_SYSTEM_DPI_AWARE = 1, PROCESS_PER_MONITOR_DPI_AWARE = 2 } PROCESS_DPI_AWARENESS;
typedef enum { MDT_EFFECTIVE_DPI = 0, MDT_ANGULAR_DPI = 1, MDT_RAW_DPI = 2, MDT_DEFAULT = MDT_EFFECTIVE_DPI } MONITOR_DPI_TYPE;
#endif
#ifndef _DPI_AWARENESS_CONTEXTS_
DECLARE_HANDLE(DPI_AWARENESS_CONTEXT);
#define DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE    (DPI_AWARENESS_CONTEXT)-3
#endif
#ifndef DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2
#define DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 (DPI_AWARENESS_CONTEXT)-4
#endif
typedef HRESULT(WINAPI* PFN_SetProcessDpiAwareness)(PROCESS_DPI_AWARENESS);                     // Shcore.lib + dll, Windows 8.1+
typedef HRESULT(WINAPI* PFN_GetDpiForMonitor)(HMONITOR, MONITOR_DPI_TYPE, UINT*, UINT*);        // Shcore.lib + dll, Windows 8.1+
typedef DPI_AWARENESS_CONTEXT(WINAPI* PFN_SetThreadDpiAwarenessContext)(DPI_AWARENESS_CONTEXT); // User32.lib + dll, Windows 10 v1607+ (Creators Update)

// Helper function to enable DPI awareness without setting up a manifest
void MsTrG_ImplWin32_EnableDpiAwareness()
{
    if (_IsWindows10OrGreater())
    {
        static HINSTANCE user32_dll = ::LoadLibraryA("user32.dll"); // Reference counted per-process
        if (PFN_SetThreadDpiAwarenessContext SetThreadDpiAwarenessContextFn = (PFN_SetThreadDpiAwarenessContext)::GetProcAddress(user32_dll, "SetThreadDpiAwarenessContext"))
        {
            SetThreadDpiAwarenessContextFn(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
            return;
        }
    }
    if (_IsWindows8Point1OrGreater())
    {
        static HINSTANCE shcore_dll = ::LoadLibraryA("shcore.dll"); // Reference counted per-process
        if (PFN_SetProcessDpiAwareness SetProcessDpiAwarenessFn = (PFN_SetProcessDpiAwareness)::GetProcAddress(shcore_dll, "SetProcessDpiAwareness"))
        {
            SetProcessDpiAwarenessFn(PROCESS_PER_MONITOR_DPI_AWARE);
            return;
        }
    }
#if _WIN32_WINNT >= 0x0600
    ::SetProcessDPIAware();
#endif
}

#if defined(_MSC_VER) && !defined(NOGDI)
#pragma comment(lib, "gdi32")   // Link with gdi32.lib for GetDeviceCaps(). MinGW will require linking with '-lgdi32'
#endif

float MsTrG_ImplWin32_GetDpiScaleForMonitor(void* monitor)
{
    UINT xdpi = 96, ydpi = 96;
    if (_IsWindows8Point1OrGreater())
    {
        static HINSTANCE shcore_dll = ::LoadLibraryA("shcore.dll"); // Reference counted per-process
        static PFN_GetDpiForMonitor GetDpiForMonitorFn = NULL;
        if (GetDpiForMonitorFn == NULL && shcore_dll != NULL)
            GetDpiForMonitorFn = (PFN_GetDpiForMonitor)::GetProcAddress(shcore_dll, "GetDpiForMonitor");
        if (GetDpiForMonitorFn != NULL)
        {
            GetDpiForMonitorFn((HMONITOR)monitor, MDT_EFFECTIVE_DPI, &xdpi, &ydpi);
            IM_ASSERT(xdpi == ydpi); // Please contact me if you hit this assert!
            return xdpi / 96.0f;
        }
    }
#ifndef NOGDI
    const HDC dc = ::GetDC(NULL);
    xdpi = ::GetDeviceCaps(dc, LOGPIXELSX);
    ydpi = ::GetDeviceCaps(dc, LOGPIXELSY);
    IM_ASSERT(xdpi == ydpi); // Please contact me if you hit this assert!
    ::ReleaseDC(NULL, dc);
#endif
    return xdpi / 96.0f;
}

float MsTrG_ImplWin32_GetDpiScaleForHwnd(void* hwnd)
{
    HMONITOR monitor = ::MonitorFromWindow((HWND)hwnd, MONITOR_DEFAULTTONEAREST);
    return MsTrG_ImplWin32_GetDpiScaleForMonitor(monitor);
}

//---------------------------------------------------------------------------------------------------------
// Transparency related helpers (optional)
//--------------------------------------------------------------------------------------------------------

#if defined(_MSC_VER)
#pragma comment(lib, "dwmapi")  // Link with dwmapi.lib. MinGW will require linking with '-ldwmapi'
#endif

// [experimental]
// Borrowed from GLFW's function updateFramebufferTransparency() in src/win32_window.c
// (the Dwm* functions are Vista era functions but we are borrowing logic from GLFW)
void MsTrG_ImplWin32_EnableAlphaCompositing(void* hwnd)
{
    if (!_IsWindowsVistaOrGreater())
        return;

    BOOL composition;
    if (FAILED(::DwmIsCompositionEnabled(&composition)) || !composition)
        return;

    BOOL opaque;
    DWORD color;
    if (_IsWindows8OrGreater() || (SUCCEEDED(::DwmGetColorizationColor(&color, &opaque)) && !opaque))
    {
        HRGN region = ::CreateRectRgn(0, 0, -1, -1);
        DWM_BLURBEHIND bb = {};
        bb.dwFlags = DWM_BB_ENABLE | DWM_BB_BLURREGION;
        bb.hRgnBlur = region;
        bb.fEnable = TRUE;
        ::DwmEnableBlurBehindWindow((HWND)hwnd, &bb);
        ::DeleteObject(region);
    }
    else
    {
        DWM_BLURBEHIND bb = {};
        bb.dwFlags = DWM_BB_ENABLE;
        ::DwmEnableBlurBehindWindow((HWND)hwnd, &bb);
    }
}

//---------------------------------------------------------------------------------------------------------
