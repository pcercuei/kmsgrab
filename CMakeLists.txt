cmake_minimum_required(VERSION 3.6)
project(kmsgrab LANGUAGES C VERSION 0.1)

include(GNUInstallDirs)
find_package(PkgConfig REQUIRED)

pkg_check_modules(PNG REQUIRED
	IMPORTED_TARGET
	libpng
)

pkg_check_modules(DRM REQUIRED
	IMPORTED_TARGET
	libdrm
)

add_executable(kmsgrab kmsgrab.c)

target_link_libraries(kmsgrab PRIVATE
	PkgConfig::PNG
	PkgConfig::DRM
)

install(TARGETS kmsgrab
	RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
