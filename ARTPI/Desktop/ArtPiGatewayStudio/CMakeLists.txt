cmake_minimum_required(VERSION 3.21)

project(ArtPiGatewayStudio VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Network Quick QuickControls2)

qt_standard_project_setup(REQUIRES 6.8)

qt_add_executable(artpi_gateway_studio
    src/main.cpp
    src/artpi_client.cpp
    src/artpi_client.h
    src/device_list_model.cpp
    src/device_list_model.h
)

qt_add_qml_module(artpi_gateway_studio
    URI ArtPiGatewayStudio
    VERSION 1.0
    QML_FILES
        qml/Main.qml
        qml/components/DeviceCard.qml
        qml/components/MetricCard.qml
        qml/components/NavButton.qml
        qml/components/Panel.qml
        qml/components/StatusPill.qml
)

target_link_libraries(artpi_gateway_studio PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Network
    Qt6::Quick
    Qt6::QuickControls2
)

target_compile_definitions(artpi_gateway_studio PRIVATE
    QT_NO_CAST_FROM_ASCII
    QT_NO_CAST_TO_ASCII
)

set_target_properties(artpi_gateway_studio PROPERTIES
    WIN32_EXECUTABLE TRUE
    MACOSX_BUNDLE TRUE
    MACOSX_BUNDLE_BUNDLE_NAME "ART-Pi Gateway Studio"
)

install(TARGETS artpi_gateway_studio
    BUNDLE DESTINATION .
    RUNTIME DESTINATION bin
)
