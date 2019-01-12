add_library(ASIO401Util_portaudio STATIC portaudio.cpp)
target_link_libraries(ASIO401Util_portaudio
	PUBLIC portaudio::portaudio
	PRIVATE dechamps_cpputil::string
)

add_library(ASIO401Util_shell STATIC shell.cpp)
