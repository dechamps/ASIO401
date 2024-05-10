#pragma once

#include <windows.h>

#include <memory>

namespace asio401 {
	
	struct WindowsHandleDeleter {
		void operator()(HANDLE handle);
	};
	using WindowsHandleUniquePtr = std::unique_ptr<void, WindowsHandleDeleter>;
	
	class WindowsReusableEvent final {
	public:
		WindowsReusableEvent();
		~WindowsReusableEvent();

		WindowsReusableEvent(const WindowsReusableEvent&) = delete;
		WindowsReusableEvent& operator=(const WindowsReusableEvent&) = delete;

		// Only one Owner can exist for a WindowsReusableEvent at any given time.
		class Owned final {
		public:
			// Event is guaranteed to be initially nonsignalled.
			Owned(WindowsReusableEvent&);
			// Event must be nonsignalled on destruction.
			~Owned();

			Owned(const Owned&) = delete;
			Owned& operator=(const Owned&) = delete;

			HANDLE getEventHandle() const { return reusableEvent.eventHandle.get(); }

		private:
			WindowsReusableEvent& reusableEvent;
		};

	private:
		const WindowsHandleUniquePtr eventHandle;
#ifndef NDEBUG
		const Owned* owner = nullptr;
#endif
	};

	class WindowsOverlappedEvent final {
	public:
		explicit WindowsOverlappedEvent(WindowsReusableEvent&);

		WindowsOverlappedEvent(const WindowsOverlappedEvent&) = delete;
		WindowsOverlappedEvent& operator=(const WindowsOverlappedEvent&) = delete;

		const WindowsReusableEvent::Owned& getOwnedReusableEvent() const { return ownedReusableEvent;  }
		OVERLAPPED& getOverlapped() { return overlapped; }

	private:
		const WindowsReusableEvent::Owned ownedReusableEvent;
		::OVERLAPPED overlapped;
	};

}
