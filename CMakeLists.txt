project(time-warp-scan VERSION 0.1.1)
set(PROJECT_FULL_NAME "Time Warp Scan")

configure_file(${CMAKE_CURRENT_SOURCE_DIR}/version.h.in ${CMAKE_CURRENT_SOURCE_DIR}/version.h)

set(time-warp-scan_HEADERS
	time-warp-scan.h
	version.h)
set(time-warp-scan_SOURCES
	time-warp-scan.c)

if(WIN32)
	get_filename_component(ISS_FILES_DIR "${CMAKE_BINARY_DIR}\\..\\package" ABSOLUTE)
	file(TO_NATIVE_PATH "${ISS_FILES_DIR}" ISS_FILES_DIR)
	get_filename_component(ISS_PACKAGE_DIR "${CMAKE_PACKAGE_PREFIX}\\.." ABSOLUTE)
	file(TO_NATIVE_PATH "${ISS_PACKAGE_DIR}" ISS_PACKAGE_DIR)
	get_filename_component(ISS_SOURCE_DIR "${PROJECT_SOURCE_DIR}" ABSOLUTE)
	file(TO_NATIVE_PATH "${ISS_SOURCE_DIR}" ISS_SOURCE_DIR)
	configure_file("installer.iss.in"
			"${PROJECT_BINARY_DIR}/installer.iss"
		)
	configure_file(resource.rc.in time-warp-scan.rc)
	list(APPEND time-warp-scan_SOURCES
		time-warp-scan.rc)
endif()

add_library(time-warp-scan MODULE
	${time-warp-scan_HEADERS}
	${time-warp-scan_SOURCES})
target_link_libraries(time-warp-scan
	libobs)

set_target_properties(time-warp-scan PROPERTIES FOLDER "plugins/exeldro")
install_obs_plugin_with_data(time-warp-scan data)
