cmake_minimum_required(VERSION 3.22)

add_subdirectory(tinyusb/src)
tinyusb_target_add(${CMAKE_PROJECT_NAME})
aux_source_directory(tinyusb/src/portable/synopsys/dwc2 tinyusb_portable_src) # select your platform tinyusb portable
aux_source_directory(tinyusb/lib/networking tinyusb_networking_src)

set(LWIP_INCLUDE_DIRS
    ${CMAKE_CURRENT_LIST_DIR}/lwip/src/include
    ${CMAKE_CURRENT_LIST_DIR}/tinyusb/src
    ${CMAKE_CURRENT_LIST_DIR})
add_subdirectory(lwip)

add_subdirectory(mf)
add_subdirectory(bootloader-web)
aux_source_directory(mf-widget widget_src)
aux_source_directory(mf-widget/http-server widget_http_server_src)

add_subdirectory(tiny-http-parser)
add_subdirectory(tiny-http-parser/test/websocket-accept)

aux_source_directory(${CMAKE_CURRENT_LIST_DIR} app_src)

target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
    ${LWIP_INCLUDE_DIRS}
)

target_sources(${CMAKE_PROJECT_NAME} PRIVATE

    # Add user sources here
    ${app_src}
    ${widget_src}
    ${widget_http_server_src}
    ${tinyusb_portable_src}
    ${tinyusb_networking_src}
)

target_link_libraries(${CMAKE_PROJECT_NAME}
    mf
    thp
    websocket_accept
    lwipcore
    lwipallapps
)
