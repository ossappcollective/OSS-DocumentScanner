cmake_minimum_required(VERSION 3.25)
project( scanner )
find_package( OpenCV REQUIRED PATHS ../../opencv/linux NO_DEFAULT_PATH)
# find_package(Tesseract PATHS ../../tesseract/linux NO_DEFAULT_PATH)
# if (NOT Tesseract_FOUND)
#     find_package(PkgConfig REQUIRED)

#     pkg_check_modules(Tesseract REQUIRED IMPORTED_TARGET tesseract lept)
#     add_library(Tesseract::libtesseract ALIAS PkgConfig::Tesseract)
# endif ()


# Find Qt (try Qt6 first, fall back to Qt5)
find_package(Qt6 COMPONENTS Core Gui Widgets Test Multimedia MultimediaWidgets Concurrent  OpenGLWidgets REQUIRED)
if(Qt6_FOUND) 
    message(STATUS "Found Qt6")
    set(QT_LIBRARIES Qt6::Core Qt6::Gui Qt6::Test Qt6::Widgets Qt6::Concurrent Qt6::OpenGLWidgets Qt6::Multimedia Qt6::MultimediaWidgets)
    set(QT_VERSION 6)
else() 
    find_package(Qt5 COMPONENTS Core Gui Widgets  Multimedia MultimediaWidgets REQUIRED)
    message(STATUS "Found Qt5")
    set(QT_LIBRARIES Qt5::Core Qt5::Gui Qt5::Widgets Qt5::Multimedia Qt5::MultimediaWidgets)
    set(QT_VERSION 5)
endif()

# adjust the debug options to output more details on stdout
add_definitions( -DVP_DEBUG -DVP_DEBUG_MODE=0 )

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_EXTENSIONS NO)
set(CMAKE_CXX_STANDARD_REQUIRED YES)

set(CMAKE_CXX_SCAN_FOR_MODULES FALSE)
set(CMAKE_CXX_COMPILER "clang++")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -stdlib=libstdc++ -std=c2x")


IF(CMAKE_COMPILER_IS_GNUCC)
  SET(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fmessage-length=0")
ENDIF(CMAKE_COMPILER_IS_GNUCC)
IF(CMAKE_COMPILER_IS_GNUCXX)
  SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fmessage-length=0")
ENDIF(CMAKE_COMPILER_IS_GNUCXX)

function(add_app target_name)
    add_executable(${target_name} ${ARGN})

    target_link_libraries(${target_name}
        ${OpenCV_LIBS}
        ${QT_LIBRARIES}
    )

    target_include_directories(${target_name} PRIVATE src/include)
endfunction()

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)

add_app(scanner
    scanner.cpp
    src/DocumentDetector.cpp
    src/WhitePaperTransform.cpp
    src/WhitePaperTransform2.cpp
    src/Utils.cpp
    src/ColorSimplificationTransform.cpp
)

add_app(signature_app
    signature_app.cpp
)

# add_executable( scanner scanner.cpp src/DocumentDetector.cpp  src/WhitePaperTransform.cpp  src/WhitePaperTransform2.cpp  src/Utils.cpp  src/ColorSimplificationTransform.cpp )
# target_link_libraries( scanner ${OpenCV_LIBS})
# target_link_libraries( scanner ${OpenCV_LIBS} Tesseract::libtesseract)
# target_include_directories(scanner PRIVATE src/include)

# Important: Add Qt include directories
# if(Qt6_FOUND)
#     target_include_directories(scanner PRIVATE ${Qt6Core_INCLUDE_DIRS} ${Qt6Gui_INCLUDE_DIRS} ${Qt6Widgets_INCLUDE_DIRS})
# else()
#     target_include_directories(scanner PRIVATE ${Qt5Core_INCLUDE_DIRS} ${Qt5Gui_INCLUDE_DIRS} ${Qt5Widgets_INCLUDE_DIRS})
# endif()

# Print Qt info for debugging
message(STATUS "Using Qt version: ${QT_VERSION}")
message(STATUS "Qt libraries: ${QT_LIBRARIES}")
if(Qt6_FOUND)
    message(STATUS "Qt6 include dirs: ${Qt6Core_INCLUDE_DIRS}")
else()
    message(STATUS "Qt5 include dirs: ${Qt5Core_INCLUDE_DIRS}")
endif()