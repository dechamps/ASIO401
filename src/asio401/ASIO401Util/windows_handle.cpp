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

	WindowsReusableEvent::WindowsReusableEvent() : eventHandle([&] {
		const auto eventHandle = ::CreateEventA(/*lpEventAttributes=*/NULL, /*bManualReset=*/TRUE, /*initialState=*/FALSE, /*lpName=*/NULL);
		if (eventHandle == NULL) throw std::runtime_error("Unable to create event handle: " + GetWindowsErrorString(GetLastError()));
		return eventHandle;
	}()) {}

	WindowsReusableEvent::~WindowsReusableEvent() {
		assert(owner == nullptr);
	}

	WindowsReusableEvent::Owned::Owned(WindowsReusableEvent& reusableEvent) : reusableEvent(reusableEvent) {
#ifndef NDEBUG
		assert(reusableEvent.owner == nullptr);
		reusableEvent.owner = this;

		assert(::WaitForSingleObject(getEventHandle(), 0) == WAIT_TIMEOUT);
#endif
	}

	WindowsReusableEvent::Owned::~Owned() {
#ifndef NDEBUG
		assert(::WaitForSingleObject(getEventHandle(), 0) == WAIT_TIMEOUT);
		assert(reusableEvent.owner == this);
		reusableEvent.owner = nullptr;
#endif
	}

	WindowsOverlappedEvent::WindowsOverlappedEvent(WindowsReusableEvent& reusableEvent) :
		ownedReusableEvent(reusableEvent) {
		overlapped = { 0 };
		overlapped.hEvent = ownedReusableEvent.getEventHandle();
	}

}
