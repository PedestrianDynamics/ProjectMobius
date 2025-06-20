cmake_minimum_required(VERSION 3.16)

project(PlotUE_Data VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 20)

find_package(Qt6 REQUIRED COMPONENTS Quick WebSockets Graphs)


qt_standard_project_setup(REQUIRES 6.9)

qt_add_executable(appPlotUE_Data
    main.cpp
)

qt_add_qml_module(appPlotUE_Data
    URI PlotUE_Data
    VERSION 1.0
    QML_FILES
    AppWindow.qml
    SOURCES WebSocketManager.h WebSocketManager.cpp
    SOURCES ChartTableModel.h ChartTableModel.cpp
        SOURCES AxisSettings.h
        SOURCES ChartSettings.h
        SOURCES MessageProcessor.h MessageProcessor.cpp
        QML_FILES GraphHoverItem.qml
        RESOURCES shaders/PlaybarShaderEffect.frag
        RESOURCES shaders/PlaybarShaderEffect.frag.qsb
)

# Qt for iOS sets MACOSX_BUNDLE_GUI_IDENTIFIER automatically since Qt 6.1.
# If you are developing for iOS or macOS you should consider setting an
# explicit, fixed bundle identifier manually though.
set_target_properties(appPlotUE_Data PROPERTIES
#    MACOSX_BUNDLE_GUI_IDENTIFIER com.example.appPlotUE_Data
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
)

target_link_libraries(appPlotUE_Data
    PRIVATE Qt6::Quick
    PRIVATE Qt6::WebSockets
    PRIVATE Qt6::Graphs
)

include(GNUInstallDirs)
install(TARGETS appPlotUE_Data
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
