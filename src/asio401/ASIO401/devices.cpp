#include "devices.h"

#include "log.h"

#include "../ASIO401Util/guid.h"
#include "../ASIO401Util/windows_error.h"

#include <SetupAPI.h>

#include <stdexcept>

namespace asio401 {

	namespace {

		struct DeviceInfoSetDeleter {
			void operator()(HDEVINFO deviceInfoSet) {
				if (::SetupDiDestroyDeviceInfoList(deviceInfoSet) != TRUE) {
					Log() << "Unable to destroy device info list: " << GetWindowsErrorString(::GetLastError());
				}
			}
		};

		using DeviceInfoSetPtr = std::unique_ptr<void, DeviceInfoSetDeleter>;

	}

	std::optional<std::string> GetDevicePath(const GUID& guid) {
		Log() << "Getting device info set for {" << GetGUIDString(guid) << "}";
		const DeviceInfoSetPtr deviceInfoSet(::SetupDiGetClassDevsA(&guid, NULL, NULL, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE));
		if (deviceInfoSet == nullptr) throw std::runtime_error("Unable to get device info set: " + GetWindowsErrorString(::GetLastError()));

		Log() << "Enumerating device interfaces";
		SP_DEVICE_INTERFACE_DATA deviceInterfaceData;
		deviceInterfaceData.cbSize = sizeof(deviceInterfaceData);
		if (::SetupDiEnumDeviceInterfaces(deviceInfoSet.get(), NULL, &guid, 0, &deviceInterfaceData) != TRUE) {
			const auto error = GetLastError();
			if (error == ERROR_NO_MORE_ITEMS) {
				Log() << "Device interface enumeration failed with ERROR_NO_MORE_ITEMS: " << GetWindowsErrorString(error);
				return std::nullopt;
			}
			throw std::runtime_error("Unable to enumerate device interfaces: " + GetWindowsErrorString(error));
		}

		Log() << "Getting device interface detail buffer size";
		DWORD deviceInterfaceDetailRequiredSize;
		if (::SetupDiGetDeviceInterfaceDetail(deviceInfoSet.get(), &deviceInterfaceData, NULL, 0, &deviceInterfaceDetailRequiredSize, NULL) == TRUE) {
			throw std::runtime_error("SetupDiGetDeviceInterfaceDetail() unexpectedly succeeded with a zero buffer size");
		}
		if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || deviceInterfaceDetailRequiredSize <= 0) {
			throw std::runtime_error("Unable to get device interface detail: " + GetWindowsErrorString(::GetLastError()));
		}

		Log() << "Getting device interface detail with buffer size " << deviceInterfaceDetailRequiredSize;
		std::vector<char> deviceInterfaceDetailBuffer(deviceInterfaceDetailRequiredSize);
		const auto deviceInterfaceDetail = reinterpret_cast<PSP_DEVICE_INTERFACE_DETAIL_DATA_A>(deviceInterfaceDetailBuffer.data());
		deviceInterfaceDetail->cbSize = sizeof(*deviceInterfaceDetail);
		if (::SetupDiGetDeviceInterfaceDetail(deviceInfoSet.get(), &deviceInterfaceData, deviceInterfaceDetail, DWORD(deviceInterfaceDetailBuffer.size()), NULL, NULL) != TRUE) {
			throw std::runtime_error("Unable to get device interface detail with buffer: " + GetWindowsErrorString(::GetLastError()));
		}

		Log() << "Device path: " << deviceInterfaceDetail->DevicePath;
		return deviceInterfaceDetail->DevicePath;
	}

}
