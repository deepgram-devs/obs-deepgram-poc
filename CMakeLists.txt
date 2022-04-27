# obs-deepgram-poc/CMakeLists.txt

project(obs-deepgram-poc)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# on macOS, `brew install openssl` will by default put things here:
if(OS_MACOS)
  set(OPENSSL_ROOT_DIR /usr/local/opt/openssl)
  set(OPENSSL_LIBRARIES /usr/local/opt/openssl/lib)
  set(OPENSSL_INCLUDE_DIR /usr/local/opt/openssl/include)
endif()

find_package(OpenSSL REQUIRED)

# on macOS, `brew install boost` will by default put things here:
if(OS_MACOS)
  SET(BOOST_ROOT /usr/local/opt/boost)
  SET(BOOST_INCLUDEDIR /usr/local/opt/boost/include)
endif()

find_package(Boost REQUIRED)

include_directories(
      "${CMAKE_CURRENT_SOURCE_DIR}/deps/websocketpp"
      "${CMAKE_CURRENT_SOURCE_DIR}/deps/json/include"
      "${OPENSSL_INCLUDE_DIR}"
      "${BOOST_INCLUDEDIR}"
      )

add_library(obs-deepgram-poc MODULE
      obs-deepgram-poc.cpp
      obs-deepgram-poc-source.cpp
      WebsocketEndpoint.hpp)

target_link_libraries(obs-deepgram-poc
      libobs
      "${OPENSSL_LIBRARIES}"
      )

install_obs_plugin_with_data(obs-deepgram-poc data)