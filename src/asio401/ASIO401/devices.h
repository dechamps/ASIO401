#pragma once

#include <rpc.h>

#include <string>
#include <unordered_set>

namespace asio401 {
	std::unordered_set<std::string> GetDevicesPaths(const GUID& guid);
}
