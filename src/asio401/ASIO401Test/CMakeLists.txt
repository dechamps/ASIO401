add_executable(ASIO401Test main.cpp ../versioninfo.rc)
target_compile_definitions(ASIO401Test PRIVATE PROJECT_DESCRIPTION="ASIO401 Self-test program")
target_link_libraries(ASIO401Test
	PRIVATE ASIOTest::ASIOTest
	PRIVATE ASIO401
	PRIVATE dechamps_CMakeUtils_version_stamp
)

install(TARGETS ASIO401Test RUNTIME DESTINATION bin)
