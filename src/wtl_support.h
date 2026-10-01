#pragma once

#pragma comment(linker, "/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <atlbase.h>
#include <atlapp.h>

extern WTL::CAppModule _Module;

#include <atlwin.h>
#include <atlframe.h>
