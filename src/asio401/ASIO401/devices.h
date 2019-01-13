#pragma once

#include <rpc.h>

#include <string>

namespace asio401 {
	std::string GetDevicePath(const GUID& guid);
}
