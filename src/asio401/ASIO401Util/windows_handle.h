#pragma once

#include <windows.h>

#include <memory>

namespace asio401 {
	
	struct WindowsHandleDeleter {
		void operator()(HANDLE handle);
	};
	using WindowsHandleUniquePtr = std::unique_ptr<void, WindowsHandleDeleter>;

	class WindowsOverlappedEvent {
	public:
		WindowsOverlappedEvent();

		OVERLAPPED& getOverlapped() { return overlapped; }

	private:
		const WindowsHandleUniquePtr eventHandle;
		OVERLAPPED overlapped;
	};

}
