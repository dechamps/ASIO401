cmake_minimum_required(VERSION 3.11)
project(ASIO401 DESCRIPTION "ASIO401 ASIO driver")

if(CMAKE_SIZEOF_VOID_P EQUAL 4)
	set(ASIO401_PLATFORM x86)
elseif(CMAKE_SIZEOF_VOID_P EQUAL 8)
	set(ASIO401_PLATFORM x64)
else()
	set(ASIO401_PLATFORM unknown)
endif()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/CMakeModules")
find_package(tinytoml MODULE REQUIRED)
find_package(dechamps_cpplog CONFIG REQUIRED)
find_package(dechamps_cpputil CONFIG REQUIRED)
find_package(dechamps_ASIOUtil CONFIG REQUIRED)
find_package(ASIOTest CONFIG REQUIRED)

set(CMAKE_CXX_STANDARD 17)
add_compile_options(
	/external:anglebrackets /WX /W4 /external:W0 /permissive- /analyze /analyze:external-

	# Suppress warnings about shadowing declarations.
	#
	# In most cases, this happens when a lambda is used to initialize some
	# variable, and the lambda declares a local variable with the same name as the
	# variable it's tasked with initializing. In such cases the shadowing is
	# actually desirable, because it prevents one from accidentally using the (not
	# yet initialized) outer variable instead of the (valid) local variable within
	# the lambda.
	/wd4458 /wd4456

	# Suppress silly "comparison of a constant with another constant" warning which rejects perfectly reasonable code such as if constexpr statements
	/wd6326

	# Work around https://developercommunity.visualstudio.com/t/1617074 (assert() statements)
	/wd4189
)
add_definitions(
	-DBUILD_CONFIGURATION="$<CONFIG>"
	-DBUILD_PLATFORM="${ASIO401_PLATFORM}"
)

add_subdirectory(../dechamps_CMakeUtils/version version EXCLUDE_FROM_ALL)

add_subdirectory(ASIO401Util EXCLUDE_FROM_ALL)
add_subdirectory(ASIO401)
add_subdirectory(ASIO401Test)
