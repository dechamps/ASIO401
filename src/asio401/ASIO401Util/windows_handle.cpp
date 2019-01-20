#include "windows_handle.h"

#include "windows_error.h"

#include <windows.h>

#include <cassert>
#include <stdexcept>

namespace asio401 {

	void WindowsHandleDeleter::operator()(HANDLE handle) {
		if (handle == INVALID_HANDLE_VALUE) return;
		const auto result = ::CloseHandle(handle);
		assert(result != 0);
	}

	WindowsOverlappedEvent::WindowsOverlappedEvent() : eventHandle([&] {
		const auto eventHandle = CreateEventA(/*lpEventAttributes=*/NULL, /*bManualReset=*/TRUE, /*initialState=*/NULL, /*lpName=*/NULL);
		if (eventHandle == NULL) throw std::runtime_error("Unable to create event handle: " + GetWindowsErrorString(GetLastError()));
		return eventHandle;
	}()) {
		overlapped = { 0 };
		overlapped.hEvent = eventHandle.get();
	}

}
