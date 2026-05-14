cmake_minimum_required(VERSION 3.20)
# Boost liefert eigenes Config-Paket – altes FindBoost-Modul nicht verwenden
if(POLICY CMP0167)
    cmake_policy(SET CMP0167 NEW)
endif()
project(dropbox-cpp CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# --- Homebrew-Pfade (Apple Silicon: /opt/homebrew, Intel: /usr/local) ---
# Kein brew-Aufruf — CLion hat brew nicht im PATH beim CMake-Configure
if(APPLE)
    if(EXISTS "/opt/homebrew")
        set(HOMEBREW_PREFIX "/opt/homebrew")   # Apple Silicon
    else()
        set(HOMEBREW_PREFIX "/usr/local")      # Intel
    endif()
    list(APPEND CMAKE_PREFIX_PATH "${HOMEBREW_PREFIX}")
endif()
# --- Abhängigkeiten ---
find_package(GTest REQUIRED)
find_package(Boost REQUIRED COMPONENTS json CONFIG)
find_library(CURL_LIB curl REQUIRED)
# --- Library: dropbox ---
add_library(dropbox STATIC
        DropboxAccountInfo.cpp
        DropboxMetadata.cpp
        DropboxRevisions.cpp
        DropboxApi.cpp
        util/HttpRequestFactory.cpp
        util/HttpRequest.cpp
        util/OAuth.cpp
        util/DropboxAuth.cpp
)
target_include_directories(dropbox PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(dropbox PRIVATE HAVE_CONFIG_H)
target_compile_options(dropbox PRIVATE -Wall -Werror -g)
target_link_libraries(dropbox PUBLIC ${CURL_LIB} Boost::json Boost::headers)
# --- Executable: tester (Integration Tests mit GTest) ---
# tester.cpp definiert sein eigenes main() → kein GTest::gtest_main verlinken
add_executable(tester tester.cpp)
target_include_directories(tester PRIVATE ${GTEST_INCLUDE_DIRS})
target_compile_definitions(tester PRIVATE HAVE_CONFIG_H)
target_compile_options(tester PRIVATE -Wall -g)
target_link_libraries(tester PRIVATE dropbox GTest::gtest pthread)
# --- Executable: unit_test (ohne GTest, ohne Netzwerk) ---
add_executable(unit_test test_unit.cpp)
target_compile_definitions(unit_test PRIVATE HAVE_CONFIG_H)
target_compile_options(unit_test PRIVATE -Wall -g)
target_link_libraries(unit_test PRIVATE dropbox)