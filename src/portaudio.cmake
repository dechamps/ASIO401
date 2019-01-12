# These CMake commands will be injected into the PortAudio CMake build.

set(ASIO401_LIST_DIR "${CMAKE_CURRENT_LIST_DIR}")

# Due to what looks like scoping issues, there are some things that can't be
# done directly from here, such as adding dependencies to source files.
# We inject ourselves into the CMake add_library() function (which does run
# into the proper scope) to work around that.
function(ADD_LIBRARY)
    _ADD_LIBRARY(${ARGV})
	if("${ASIO401_INJECTED}")
		return()
	endif()
	set(ASIO401_INJECTED 1 PARENT_SCOPE)
	message(STATUS "Running ASIO401 portaudio.cmake injection")

	find_package(Git)
	set(ASIO401_VERSION_FILE "${CMAKE_CURRENT_BINARY_DIR}/asio401_version_stamp.h")
	add_custom_target(asio401_version_stamp_gen
		COMMAND "${CMAKE_COMMAND}"
			-DPORTAUDIO_LIST_DIR="${CMAKE_CURRENT_LIST_DIR}"
			-DOUTPUT_HEADER_FILE="${ASIO401_VERSION_FILE}"
			-DGit_FOUND="${Git_FOUND}"
			-DGIT_EXECUTABLE="${GIT_EXECUTABLE}"
			-P "${ASIO401_LIST_DIR}/portaudio_version_stamp.cmake"
	)
	set_property(SOURCE src/common/pa_front.c APPEND PROPERTY OBJECT_DEPENDS asio401_version_stamp_gen)
	set_property(SOURCE src/common/pa_front.c APPEND PROPERTY COMPILE_OPTIONS "/FI${ASIO401_VERSION_FILE}")
endfunction()
