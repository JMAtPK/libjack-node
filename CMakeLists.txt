cmake_minimum_required(VERSION 3.15)
cmake_policy(SET CMP0091 NEW)
cmake_policy(SET CMP0042 NEW)

project(libjack_node)

# Find JACK — prefer PipeWire's libjack when available
# PipeWire's JACK library lives in a subdirectory and provides the same ABI.
# Headers come from libjack-jackd2-dev regardless.

set(PIPEWIRE_JACK_PATHS
  "/usr/lib/x86_64-linux-gnu/pipewire-0.3/jack"
  "/usr/lib/aarch64-linux-gnu/pipewire-0.3/jack"
  "/usr/lib64/pipewire-0.3/jack"
  "/usr/lib/pipewire-0.3/jack"
)

set(JACK_LIBRARY "")
foreach(PW_PATH ${PIPEWIRE_JACK_PATHS})
  if(EXISTS "${PW_PATH}/libjack.so")
    set(JACK_LIBRARY "${PW_PATH}/libjack.so")
    message(STATUS "Using PipeWire JACK: ${JACK_LIBRARY}")
    break()
  endif()
endforeach()

if(NOT JACK_LIBRARY)
  find_library(JACK_LIBRARY NAMES jack)
  if(JACK_LIBRARY)
    message(STATUS "Using system JACK: ${JACK_LIBRARY}")
  endif()
endif()

find_path(JACK_INCLUDE_DIR NAMES jack/jack.h)

if(NOT JACK_LIBRARY OR NOT JACK_INCLUDE_DIR)
  message(FATAL_ERROR
    "JACK development files not found.\n"
    "Install with: sudo apt install pipewire-jack libjack-jackd2-dev   (Debian/Ubuntu)\n"
    "              sudo dnf install pipewire-jack-audio-connection-kit-devel   (Fedora)\n"
    "              sudo pacman -S pipewire-jack                                (Arch)")
endif()

message(STATUS "JACK headers: ${JACK_INCLUDE_DIR}")

# node-addon-api
include_directories(${CMAKE_JS_INC})
file(GLOB SOURCE_FILES "src/*.cpp")

add_library(${PROJECT_NAME} SHARED ${SOURCE_FILES} ${CMAKE_JS_SRC})
set_target_properties(${PROJECT_NAME} PROPERTIES PREFIX "" SUFFIX ".node")
target_link_libraries(${PROJECT_NAME} ${CMAKE_JS_LIB} ${JACK_LIBRARY})
target_include_directories(${PROJECT_NAME} PRIVATE ${JACK_INCLUDE_DIR})

# node-addon-api without exceptions (faster, simpler)
execute_process(
  COMMAND node -p "require('node-addon-api').include"
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  OUTPUT_VARIABLE NODE_ADDON_API_DIR
  OUTPUT_STRIP_TRAILING_WHITESPACE
)
string(REPLACE "\"" "" NODE_ADDON_API_DIR ${NODE_ADDON_API_DIR})
target_include_directories(${PROJECT_NAME} PRIVATE ${NODE_ADDON_API_DIR})
target_compile_definitions(${PROJECT_NAME} PRIVATE NAPI_VERSION=8 NAPI_DISABLE_CPP_EXCEPTIONS)
