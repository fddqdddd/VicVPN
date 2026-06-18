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
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static-libgcc -static-libstdc++")
    set(CMAKE_RC_FLAGS "${CMAKE_RC_FLAGS} -I${CMAKE_SOURCE_DIR}/resources")
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

target_compile_definitions(VicVPN PRIVATE VICVPN_CHANNEL="beta")

target_link_libraries(VicVPN PRIVATE
    Qt6::Widgets
    Qt6::Network
    Qt6::Svg
    nlohmann_json::nlohmann_json
    sqlite3
)

if(WIN32)
    target_link_libraries(VicVPN PRIVATE ws2_32 iphlpapi shell32 advapi32)
    target_include_directories(VicVPN PRIVATE C:/msys64/ucrt64/include)
endif()

if(WIN32)
    add_custom_command(TARGET VicVPN POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:VicVPN>/core
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_SOURCE_DIR}/third_party/wintun/wintun.dll
            $<TARGET_FILE_DIR:VicVPN>/core/wintun.dll
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_SOURCE_DIR}/third_party/wintun/LICENSE-wintun.txt
            $<TARGET_FILE_DIR:VicVPN>/core/LICENSE-wintun.txt
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/resources/styles
            $<TARGET_FILE_DIR:VicVPN>/styles
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_SOURCE_DIR}/resources/styles/dark.qss
            $<TARGET_FILE_DIR:VicVPN>/styles/dark.qss
        COMMENT "Copy runtime assets"
    )
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
