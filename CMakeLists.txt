cmake_minimum_required(VERSION 3.16)
project(VicVPN VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

configure_file(
    ${CMAKE_SOURCE_DIR}/include/vicvpn/app/Version.h.in
    ${CMAKE_BINARY_DIR}/generated/vicvpn/app/Version.h
    @ONLY
)

if(MINGW)
    enable_language(RC)
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static-libgcc -static-libstdc++ -Wl,-Bstatic -lwinpthread -Wl,-Bdynamic")
    set(CMAKE_RC_FLAGS "${CMAKE_RC_FLAGS} -I${CMAKE_SOURCE_DIR}/resources")
    add_compile_definitions(WINVER=0x0A00 _WIN32_WINNT=0x0A00 NTDDI_VERSION=0x0A000000)
endif()

find_package(Qt6 REQUIRED COMPONENTS Widgets Network Svg)
find_package(nlohmann_json CONFIG REQUIRED)

set(VICVPN_HEADERS
    include/vicvpn/ui/MainWindow.h
    include/vicvpn/ui/ImportDialog.h
    include/vicvpn/ui/SettingsDialog.h
    include/vicvpn/ui/AboutDialog.h
    include/vicvpn/core/ConnectionManager.h
    include/vicvpn/core/CoreManagers.h
    include/vicvpn/app/Settings.h
    include/vicvpn/storage/Database.h
)

set(VICVPN_SOURCES
    src/main.cpp
    src/app/Application.cpp
    src/app/I18n.cpp
    src/app/Settings.cpp
    src/ui/MainWindow.cpp
    src/ui/ImportDialog.cpp
    src/ui/SettingsDialog.cpp
    src/ui/AboutDialog.cpp
    src/core/ConnectionManager.cpp
    src/core/CoreHealth.cpp
    src/core/XrayCoreManager.cpp
    src/core/HysteriaCoreManager.cpp
    src/core/SingboxCoreManager.cpp
    src/core/Tun2socksManager.cpp
    src/config/XrayConfigBuilder.cpp
    src/config/SingboxConfigBuilder.cpp
    src/config/Hy2ConfigBuilder.cpp
    src/parser/UriParser.cpp
    src/parser/Hy2UriParser.cpp
    src/parser/SubscriptionFetcher.cpp
    src/parser/SsconfResolver.cpp
    src/parser/SingboxConverter.cpp
    src/parser/OutlineConfigParser.cpp
    src/parser/SsconfCountry.cpp
    src/parser/ImportService.cpp
    src/parser/ParseLog.cpp
    src/update/UpdateChecker.cpp
    src/ui/UpdateDialog.cpp
    src/tunnel/SshTunnel.cpp
    src/storage/Database.cpp
    src/model/ServerProfile.cpp
    src/util/AppPaths.cpp
    src/util/StringUtil.cpp
    resources/resources.qrc
)

if(WIN32)
    list(APPEND VICVPN_SOURCES
        src/util/AdminElevation.cpp
        src/util/TunWindows.cpp
        src/util/NetworkRoute.cpp
        src/util/PlatformAutostart_win.cpp
        src/util/WindowsAutostart.cpp
        resources/app.rc
    )
else()
    list(APPEND VICVPN_SOURCES
        src/util/AdminElevation_linux.cpp
        src/util/TunLinux.cpp
        src/util/NetworkRoute_linux.cpp
        src/util/PlatformAutostart_linux.cpp
    )
endif()

if(WIN32)
    add_executable(VicVPN WIN32 ${VICVPN_SOURCES} ${VICVPN_HEADERS})
    set_target_properties(VicVPN PROPERTIES WIN32_EXECUTABLE TRUE)
else()
    add_executable(VicVPN ${VICVPN_SOURCES} ${VICVPN_HEADERS})
endif()

set_target_properties(VicVPN PROPERTIES OUTPUT_NAME "VicVPN")

target_include_directories(VicVPN PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_BINARY_DIR}/generated
)

# Build identity so users can tell which commit produced a running binary.
# This is what the import log prints on startup.
find_package(Git QUIET)
set(VICVPN_BUILD_ID "dev")
if(GIT_FOUND)
    execute_process(
        COMMAND ${GIT_EXECUTABLE} rev-parse --short HEAD
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        OUTPUT_VARIABLE VICVPN_GIT_SHA
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
    if(VICVPN_GIT_SHA)
        set(VICVPN_BUILD_ID "${VICVPN_GIT_SHA}")
        # Only tag local edits. A release build has to be distinguishable from
        # a work-in-progress one, otherwise the marker tells us nothing.
        execute_process(
            COMMAND ${GIT_EXECUTABLE} status --porcelain --untracked-files=no
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            OUTPUT_VARIABLE VICVPN_GIT_DIRTY
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET
        )
        if(VICVPN_GIT_DIRTY)
            set(VICVPN_BUILD_ID "${VICVPN_BUILD_ID}-dirty")
        endif()
    endif()
endif()
message(STATUS "VicVPN build id: ${VICVPN_BUILD_ID}")

target_compile_definitions(VicVPN PRIVATE
    VICVPN_CHANNEL="alpha"
    VICVPN_BUILD_ID="${VICVPN_BUILD_ID}"
    VICVPN_REPO_OWNER="fddqdddd"
    VICVPN_REPO_NAME="VicVPN"
)

target_link_libraries(VicVPN PRIVATE
    Qt6::Widgets
    Qt6::Network
    Qt6::Svg
    nlohmann_json::nlohmann_json
)

if(WIN32)
    target_link_libraries(VicVPN PRIVATE ws2_32 iphlpapi shell32 advapi32)
    target_include_directories(VicVPN PRIVATE C:/msys64/ucrt64/include)

    # Prefer static libs (libsqlite3.a) over import libs (libsqlite3.dll.a)
    set(SQLITE3_A "C:/msys64/ucrt64/lib/libsqlite3.a")
    if(EXISTS "${SQLITE3_A}")
        message(STATUS "Linking sqlite3 statically: ${SQLITE3_A}")
        target_link_libraries(VicVPN PRIVATE "${SQLITE3_A}")
    else()
        target_link_libraries(VicVPN PRIVATE sqlite3)
    endif()

    set(WINPTHREAD_A "C:/msys64/ucrt64/lib/libwinpthread.a")
    if(EXISTS "${WINPTHREAD_A}")
        message(STATUS "Linking winpthread statically: ${WINPTHREAD_A}")
        target_link_libraries(VicVPN PRIVATE "${WINPTHREAD_A}")
    endif()

    set(LIBSSH2_A "C:/msys64/ucrt64/lib/libssh2.dll.a")
    if(EXISTS "${LIBSSH2_A}")
        message(STATUS "Linking libssh2: ${LIBSSH2_A}")
        target_link_libraries(VicVPN PRIVATE "${LIBSSH2_A}")
    else()
        message(FATAL_ERROR "libssh2 not found in C:/msys64/ucrt64 (pacman -S mingw-w64-ucrt-x86_64-libssh2)")
    endif()
endif()

if(WIN32)
    add_custom_command(TARGET VicVPN POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:VicVPN>/core
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/resources/styles
            $<TARGET_FILE_DIR:VicVPN>/styles
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_SOURCE_DIR}/resources/styles/dark.qss
            $<TARGET_FILE_DIR:VicVPN>/styles/dark.qss
        COMMENT "Copy runtime assets"
    )
    if(EXISTS "${CMAKE_SOURCE_DIR}/third_party/wintun/LICENSE-wintun.txt")
        add_custom_command(TARGET VicVPN POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${CMAKE_SOURCE_DIR}/third_party/wintun/LICENSE-wintun.txt
                $<TARGET_FILE_DIR:VicVPN>/core/LICENSE-wintun.txt
            COMMENT "Copy LICENSE-wintun.txt"
        )
    endif()
    if(EXISTS "${CMAKE_SOURCE_DIR}/third_party/wintun/wintun.dll")
        add_custom_command(TARGET VicVPN POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${CMAKE_SOURCE_DIR}/third_party/wintun/wintun.dll
                $<TARGET_FILE_DIR:VicVPN>/core/wintun.dll
            COMMENT "Copy wintun.dll"
        )
    endif()
else()
    add_custom_command(TARGET VicVPN POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/resources/styles
            $<TARGET_FILE_DIR:VicVPN>/styles
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_SOURCE_DIR}/resources/styles/dark.qss
            $<TARGET_FILE_DIR:VicVPN>/styles/dark.qss
        COMMENT "Copy runtime assets"
    )
endif()

install(TARGETS VicVPN RUNTIME DESTINATION bin)
install(DIRECTORY resources/styles DESTINATION share/vicvpn)

option(VICVPN_BUILD_TESTS "Build parser smoke tests" OFF)
if(VICVPN_BUILD_TESTS)
    add_executable(vicvpn_parser_smoke
        tests/parser_smoke.cpp
        src/parser/UriParser.cpp
        src/parser/Hy2UriParser.cpp
        src/parser/SubscriptionFetcher.cpp
        src/parser/SsconfResolver.cpp
        src/parser/SingboxConverter.cpp
        src/parser/OutlineConfigParser.cpp
        src/parser/SsconfCountry.cpp
        src/parser/ImportService.cpp
        src/parser/ParseLog.cpp
        src/update/UpdateChecker.cpp
        src/app/I18n.cpp
        src/model/ServerProfile.cpp
        src/util/StringUtil.cpp
        src/util/AppPaths.cpp
    )
    target_include_directories(vicvpn_parser_smoke PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_BINARY_DIR}/generated
    )
    target_link_libraries(vicvpn_parser_smoke PRIVATE Qt6::Core Qt6::Network nlohmann_json::nlohmann_json)
    target_compile_definitions(vicvpn_parser_smoke PRIVATE
        VICVPN_BUILD_ID="${VICVPN_BUILD_ID}"
        VICVPN_REPO_OWNER="fddqdddd"
        VICVPN_REPO_NAME="VicVPN"
    )
    if(WIN32)
        target_include_directories(vicvpn_parser_smoke PRIVATE C:/msys64/ucrt64/include)
        target_link_libraries(vicvpn_parser_smoke PRIVATE C:/msys64/ucrt64/lib/libsqlite3.a)
    endif()
endif()
