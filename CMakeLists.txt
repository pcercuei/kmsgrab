cmake_minimum_required(VERSION 3.4)
project(kmsgrab LANGUAGES C VERSION 0.1)

find_package(PkgConfig REQUIRED)

pkg_check_modules(PNG QUIET libpng)
if (NOT PNG_FOUND)
	message(FATAL_ERROR "Missing dependency: libpng")
endif()

pkg_check_modules(DRM QUIET libdrm)
if (NOT DRM_FOUND)
	message(FATAL_ERROR "Missing dependency: libdrm")
endif()

add_executable(kmsgrab kmsgrab.c)

target_include_directories(kmsgrab PRIVATE
	${PNG_INCLUDE_DIRS}
	${DRM_INCLUDE_DIRS}
)

target_link_directories(kmsgrab PRIVATE
	${PNG_LIBRARY_DIRS}
	${DRM_LIBRARY_DIRS}
)

target_link_libraries(kmsgrab PRIVATE
	${PNG_LIBRARIES}
	${DRM_LIBRARIES}
)
