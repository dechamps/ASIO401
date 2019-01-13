#pragma once 

#include <windows.h>

#include <string>

namespace asio401 {

	std::string GetWindowsErrorString(DWORD error);

}