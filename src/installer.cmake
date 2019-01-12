include(check_git_submodule.cmake)
check_git_submodule(dechamps_CMakeUtils)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/dechamps_CMakeUtils")
find_package(InnoSetup MODULE REQUIRED)

find_package(Git MODULE REQUIRED)
set(DECHAMPS_CMAKEUTILS_GIT_DIR "${CMAKE_CURRENT_LIST_DIR}/asio401")
include(version/version)
string(REGEX REPLACE "^asio401-" "" ASIO401_VERSION "${DECHAMPS_CMAKEUTILS_GIT_DESCRIPTION_DIRTY}")

string(TIMESTAMP ASIO401_BUILD_TIMESTAMP "%Y-%m-%dT%H%M%SZ" UTC)
string(RANDOM ASIO401_BUILD_ID)
if (NOT DEFINED ASIO401_BUILD_ROOT_DIR)
    set(ASIO401_BUILD_ROOT_DIR "$ENV{USERPROFILE}/CMakeBuilds/ASIO401-${ASIO401_BUILD_TIMESTAMP}-${ASIO401_BUILD_ID}")
endif()
message(STATUS "ASIO401 build root directory: ${ASIO401_BUILD_ROOT_DIR}")

file(MAKE_DIRECTORY "${ASIO401_BUILD_ROOT_DIR}/x64" "${ASIO401_BUILD_ROOT_DIR}/x86")

include(build_msvc)
build_msvc(SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}" BUILD_DIR "${ASIO401_BUILD_ROOT_DIR}/x64" ARCH amd64)
build_msvc(SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}" BUILD_DIR "${ASIO401_BUILD_ROOT_DIR}/x86" ARCH x86)

file(GLOB ASIO401_DOC_FILES LIST_DIRECTORIES FALSE "${CMAKE_CURRENT_LIST_DIR}/../*.txt" "${CMAKE_CURRENT_LIST_DIR}/../*.md")
file(INSTALL ${ASIO401_DOC_FILES} DESTINATION "${ASIO401_BUILD_ROOT_DIR}")

configure_file("${CMAKE_CURRENT_LIST_DIR}/installer.in.iss" "${ASIO401_BUILD_ROOT_DIR}/installer.iss" @ONLY)
include(execute_process_or_die)
execute_process_or_die(
    COMMAND "${InnoSetup_iscc_EXECUTABLE}" "${ASIO401_BUILD_ROOT_DIR}/installer.iss" /O. /FASIO401-${ASIO401_VERSION}
    WORKING_DIRECTORY "${ASIO401_BUILD_ROOT_DIR}"
)
