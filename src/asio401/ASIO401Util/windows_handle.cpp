#include "windows_handle.h"

#include <windows.h>

#include <cassert>

namespace asio401 {

	void WindowsHandleDeleter::operator()(HANDLE handle) {
		if (handle == INVALID_HANDLE_VALUE) return;
		const auto result = ::CloseHandle(handle);
		assert(result != 0);
	}

}
