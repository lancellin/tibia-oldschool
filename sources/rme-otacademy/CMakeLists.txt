cmake_minimum_required(VERSION 3.16)

if(POLICY CMP0091)
	cmake_policy(SET CMP0091 NEW)
endif()

project(rme)

if(NOT CMAKE_BUILD_TYPE)
	set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(MSVC)
	add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
	add_compile_options(/bigobj)
	set(CMAKE_CXX_FLAGS_DEBUG "/Od /Zi /D__DEBUG__")
	set(CMAKE_CXX_FLAGS_MINSIZEREL "/O1 /DNDEBUG")
	set(CMAKE_CXX_FLAGS_RELEASE "/O2 /DNDEBUG")
	set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "/O2 /Zi")
	set(wxWidgets_USE_STATIC ON)
else()
	set(CMAKE_CXX_FLAGS                "-Wall -Wextra -Wno-unused-parameter -Wno-unused-variable -Wno-deprecated-declarations -Wno-overloaded-virtual -std=c++0x -Wno-strict-aliasing -Wno-sign-compare -Wno-unused-function -Wunused-result")
	set(CMAKE_CXX_FLAGS_DEBUG          "-O0 -g -D__DEBUG__")
	set(CMAKE_CXX_FLAGS_MINSIZEREL     "-Os -DNDEBUG")
	set(CMAKE_CXX_FLAGS_RELEASE        "-O4 -DNDEBUG")
	set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "-O2 -g")
endif()

find_package(OpenGL REQUIRED)

if(APPLE)
    set(CMAKE_PREFIX_PATH /usr/local/opt/libarchive)
endif()
find_package(LibArchive REQUIRED)

if(WIN32)
    set(Boost_THREADAPI win32)
endif()
find_package(Boost 1.34.0 CONFIG REQUIRED COMPONENTS thread system)

find_package(wxWidgets COMPONENTS html aui gl adv core net base REQUIRED)

find_package(GLUT REQUIRED)

include(${wxWidgets_USE_FILE})
include(source/CMakeLists.txt)
add_executable(rme WIN32 ${rme_H} ${rme_SRC})
target_compile_definitions(rme PRIVATE BOOST_BIND_GLOBAL_PLACEHOLDERS)

if(MSVC)
	set_property(TARGET rme PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

if(TARGET GLUT::GLUT)
	set(RME_GLUT_LIB GLUT::GLUT)
else()
	set(RME_GLUT_LIB ${GLUT_LIBRARIES})
endif()

include_directories(${Boost_INCLUDE_DIRS} ${LibArchive_INCLUDE_DIRS} ${OPENGL_INCLUDE_DIR} ${GLUT_INCLUDE_DIRS})
target_link_libraries(rme ${wxWidgets_LIBRARIES} Boost::thread Boost::system ${LibArchive_LIBRARIES} ${OPENGL_LIBRARIES} ${RME_GLUT_LIB})
