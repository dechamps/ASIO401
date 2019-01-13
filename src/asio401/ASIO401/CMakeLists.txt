if(CMAKE_SIZEOF_VOID_P EQUAL 4)
	set(ASIO401_MIDL_ENV_FLAG /env win32)
elseif(CMAKE_SIZEOF_VOID_P EQUAL 8)
	set(ASIO401_MIDL_ENV_FLAG /env amd64)
else()
	set(ASIO401_MIDL_ENV_FLAG)
endif()

add_custom_command(
	OUTPUT asio401_h.h asio401_i.c asio401.tlb
	COMMAND midl /nologo /header asio401_h.h ${ASIO401_MIDL_ENV_FLAG} "${CMAKE_CURRENT_LIST_DIR}/asio401.idl"
	MAIN_DEPENDENCY asio401.idl
)
add_library(ASIO401_idl INTERFACE)
target_sources(ASIO401_idl
	INTERFACE "${CMAKE_CURRENT_BINARY_DIR}/asio401_h.h"
	INTERFACE "${CMAKE_CURRENT_BINARY_DIR}/asio401.tlb"
)
target_include_directories(ASIO401_idl INTERFACE "${CMAKE_CURRENT_BINARY_DIR}")

add_library(ASIO401_casio401 STATIC EXCLUDE_FROM_ALL casio401.cpp)
target_link_libraries(ASIO401_casio401
	PRIVATE ASIO401_asio401
	PRIVATE ASIO401_idl
	PRIVATE ASIO401_log
	PRIVATE ASIO401Util_shell
	PRIVATE dechamps_ASIOUtil::asiosdk_iasiodrv
	PRIVATE dechamps_ASIOUtil::asio
)

add_library(ASIO401_comdll STATIC EXCLUDE_FROM_ALL comdll.cpp)
target_compile_definitions(ASIO401_comdll PRIVATE _WINDLL)

add_library(ASIO401_config STATIC EXCLUDE_FROM_ALL config.cpp)
target_link_libraries(ASIO401_config
	PRIVATE ASIO401_log
	PRIVATE ASIO401Util_shell
	PRIVATE tinytoml
)

add_library(ASIO401_log STATIC EXCLUDE_FROM_ALL log.cpp)
target_link_libraries(ASIO401_log
	PUBLIC dechamps_cpplog::log
	PRIVATE ASIO401Util_shell
	PRIVATE dechamps_CMakeUtils_version
)

add_library(ASIO401_devices STATIC EXCLUDE_FROM_ALL devices.cpp)
target_link_libraries(ASIO401_devices
	PRIVATE ASIO401_log
	PRIVATE ASIO401Util_guid
	PRIVATE ASIO401Util_windows_error
	PRIVATE SetupAPI
)

add_library(ASIO401_winusb STATIC EXCLUDE_FROM_ALL winusb.cpp)
target_link_libraries(ASIO401_winusb
	PRIVATE ASIO401_log
	PRIVATE ASIO401Util_windows_error
	PRIVATE ASIO401Util_windows_handle
	PRIVATE winusb
)

add_library(ASIO401_qa401 STATIC EXCLUDE_FROM_ALL qa401.cpp)
target_link_libraries(ASIO401_qa401
	PUBLIC ASIO401_winusb
	PRIVATE ASIO401_log
	PRIVATE ASIO401Util_windows_error
	PRIVATE winusb
)

add_library(ASIO401_asio401 STATIC EXCLUDE_FROM_ALL asio401.cpp)
target_link_libraries(ASIO401_asio401
	PUBLIC dechamps_ASIOUtil::asiosdk_asioh
	PUBLIC dechamps_ASIOUtil::asiosdk_asiosys
	PUBLIC ASIO401_config
	PUBLIC ASIO401_qa401
	PRIVATE dechamps_ASIOUtil::asio
	PRIVATE ASIO401_devices
	PRIVATE ASIO401_log
	PRIVATE dechamps_cpputil::endian
	PRIVATE dechamps_cpputil::string
	PRIVATE dechamps_CMakeUtils_version
	PRIVATE winmm
)

# Note: this is SHARED, not MODULE, otherwise CMake refuses to link that in ASIO401Test.
add_library(ASIO401 SHARED dll.def asio401.rc ../versioninfo.rc)
target_compile_definitions(ASIO401 PRIVATE PROJECT_DESCRIPTION="ASIO401 ASIO Driver DLL")
target_link_libraries(ASIO401
	PRIVATE ASIO401_casio401
	PRIVATE dechamps_CMakeUtils_version_stamp
	PRIVATE ASIO401_comdll
	PRIVATE ASIO401_idl
)
install(TARGETS ASIO401 RUNTIME DESTINATION bin)
