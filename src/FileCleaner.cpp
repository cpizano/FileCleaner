// FileCleaner.cpp
#define NOMINMAX

#include <windows.h>
#include "..\Resource.h"

extern "C" IMAGE_DOS_HEADER __ImageBase;

HINSTANCE ThisModule() {
  return reinterpret_cast<HINSTANCE>(&__ImageBase);
}

plx::File OpenConfigFile() {
  std::unique_ptr<wchar_t[]> sp(new wchar_t[300]);
  ::GetModuleFileNameW(ThisModule(), sp.get(), 256);
  auto path = plx::FilePath(sp.get()).parent().append(L"config.json");
  plx::FileParams fparams = plx::FileParams::Read_SharedRead();
  return plx::File::Create(path, fparams, plx::FileSecurity());
}

plx::JsonValue ReadConfig(plx::File& cfile) {
  if (!cfile.is_valid())
    return false;
  auto size = cfile.size_in_bytes();
  if (size < 10)
    return false;
  plx::Range<char> r(0, size);
  auto mem = plx::HeapRange(r);
  if (cfile.read(r, 0) != size)
    return false;
  plx::Range<const char> json(r);
  return plx::ParseJsonValue(json);
}

plx::File OpenDirectory(plx::JsonValue& config) {
  auto path_ns = config["path_to_clean"].get_string();
  plx::FilePath path(std::wstring(path_ns.begin(), path_ns.end()));

  auto dir_par = plx::FileParams::Directory_ShareAll();
  return plx::File::Create(path, dir_par, plx::FileSecurity());
}

bool EnumAndClean(plx::File& dir) {
  if (!dir.is_valid())
    return false;
  if (dir.status() != (plx::File::directory | plx::File::existing))
    return false;
  // Do real work here.
  return true;
}

bool CleanFiles(HWND window) {
  plx::File cfile = OpenConfigFile();
  auto config = ReadConfig(cfile);

  auto dir = OpenDirectory(config);
  if (!EnumAndClean(dir))
    return false;

  int timer_ms = 1000 * 60 * plx::To<int>(config["check_frequency"].get_int64());
  ::SetTimer(window, 1007, timer_ms , nullptr);
  return true;
}

template <typename T, typename U>
T VerifyNot(T actual, U error) {
  if (actual != error)
    return actual;

  volatile ULONG err = ::GetLastError();
  __debugbreak();
  __assume(0);
}

typedef LRESULT (* MsgCallBack)(HWND, WPARAM, LPARAM);
struct MessageHandler {
  UINT message;
  MsgCallBack callback;
};

HWND MakeWindow(
    const wchar_t* title, ULONG style, HMENU menu, const SIZE& size, MessageHandler* handlers) {
  WNDCLASSEXW wcex = {sizeof(wcex)};
  wcex.hCursor = ::LoadCursorW(NULL, IDC_ARROW);
  wcex.hInstance = ThisModule();
  wcex.hIcon = ::LoadIcon(ThisModule(), MAKEINTRESOURCE(IDI_FILECLEANER));
  wcex.lpszMenuName	= MAKEINTRESOURCE(IDC_FILECLEANER);
  wcex.lpszClassName = __FILEW__;
  wcex.lpfnWndProc = [] (HWND window, UINT message, WPARAM wparam, LPARAM lparam) -> LRESULT {
    static MessageHandler* s_handlers = reinterpret_cast<MessageHandler*>(lparam);
    size_t ix = 0;
    while (s_handlers[ix].message != -1) {
      if (s_handlers[ix].message == message)
        return s_handlers[ix].callback(window, wparam, lparam);
      ++ix;
    }

    return ::DefWindowProcW(window, message, wparam, lparam);
  };

  wcex.lpfnWndProc(NULL, 0, 0, reinterpret_cast<UINT_PTR>(handlers));
  ATOM atom = VerifyNot(::RegisterClassExW(&wcex), 0);
  int pos_def = CW_USEDEFAULT;
  return ::CreateWindowExW(0, MAKEINTATOM(atom), title, style,
                           pos_def, pos_def, size.cx, size.cy,
                           NULL, menu, ThisModule(), NULL); 
}

int __stdcall wWinMain(HINSTANCE module, HINSTANCE, wchar_t* cc, int) {

  static int count = 0;
  static HPEN pen_dot = ::CreatePen(PS_DOT, 1, RGB(0, 0, 0));

  MessageHandler msg_handlers[] = {
    { WM_PAINT, [] (HWND window, WPARAM, LPARAM) -> LRESULT {
      PAINTSTRUCT ps;
      HDC dc = ::BeginPaint(window, &ps);
      ::Rectangle(dc, 10, 10, 100, 100);
      ::EndPaint(window, &ps);
      ++count;
      return 0;
    }},

    { WM_ERASEBKGND, [] (HWND window, WPARAM wparam, LPARAM) -> LRESULT {
      RECT rect;
      VerifyNot(::GetClientRect(window, &rect), FALSE);
      HGDIOBJ prev_obj = ::SelectObject(HDC(wparam), pen_dot);
      ::Rectangle(HDC(wparam), rect.left, rect.top, rect.right, rect.bottom);
      ::SelectObject(HDC(wparam),  prev_obj);
      return 0;
    }},

    { WM_DISPLAYCHANGE, [] (HWND window, WPARAM, LPARAM) -> LRESULT {
      ::InvalidateRect(window, NULL, TRUE);
      return 0;
    }},

    { WM_CLOSE, [] (HWND window, WPARAM, LPARAM) -> LRESULT {
      ::PostQuitMessage(0);
      return 0;
    }},

    { WM_ENDSESSION, [] (HWND window, WPARAM, LPARAM) -> LRESULT {
      ::PostQuitMessage(0);
      return 0;
    }},

    { WM_TIMER, [] (HWND window, WPARAM wp, LPARAM) -> LRESULT {
      ::KillTimer(window, wp);
      CleanFiles(window);
      return 0;
    }},

    {-1, NULL}
  };

  SIZE size = {300, 200};
  HWND main_window = VerifyNot(MakeWindow(
      L"file cleaner", WS_OVERLAPPEDWINDOW | WS_VISIBLE, NULL, size, msg_handlers), HWND(NULL));

  CleanFiles(main_window);

  MSG msg;
  while (VerifyNot(::GetMessageW(&msg, NULL, 0, 0), -1)) {
    ::DispatchMessageW(&msg);
  }

  return 0;
}

