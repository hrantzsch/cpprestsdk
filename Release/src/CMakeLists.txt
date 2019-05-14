cmake_policy(SET CMP0022 NEW)

file(GLOB HEADERS_PPLX "../include/pplx/*.h" "../include/pplx/*.hpp")

set(HEADERS_CPPREST
  ../include/cpprest/astreambuf.h
  ../include/cpprest/asyncrt_utils.h
  ../include/cpprest/base_uri.h
  ../include/cpprest/client_authentication_info.h
  ../include/cpprest/containerstream.h
  ../include/cpprest/filestream.h
  ../include/cpprest/http_client.h
  ../include/cpprest/http_compression.h
  ../include/cpprest/http_headers.h
  ../include/cpprest/http_msg.h
  ../include/cpprest/interopstream.h
  ../include/cpprest/json.h
  ../include/cpprest/oauth1.h
  ../include/cpprest/oauth2.h
  ../include/cpprest/producerconsumerstream.h
  ../include/cpprest/rawptrstream.h
  ../include/cpprest/streams.h
  ../include/cpprest/uri_builder.h
  ../include/cpprest/uri.h
  ../include/cpprest/version.h
  ../include/cpprest/ws_client.h
  ../include/cpprest/ws_msg.h
)

set(HEADERS_DETAILS
  ../include/cpprest/details/basic_types.h
  ../include/cpprest/details/cpprest_compat.h
  ../include/cpprest/details/fileio.h
  ../include/cpprest/details/http_helpers.h
  ../include/cpprest/details/nosal.h
  ../include/cpprest/details/resource.h
  ../include/cpprest/details/web_utilities.h
  ../include/cpprest/details/SafeInt3.hpp
  ../include/cpprest/details/http_constants.dat
)

source_group("Header Files\\cpprest" FILES ${HEADERS_CPPREST})
source_group("Header Files\\pplx" FILES ${HEADERS_PPLX})
source_group("Header Files\\cpprest\\details" FILES ${HEADERS_DETAILS})

file(GLOB HEADER_PPLX_THREADPOOL "../include/pplx/threadpool.h")

list(REMOVE_ITEM HEADERS_PPLX ${HEADER_PPLX_THREADPOOL})

set(SOURCES
  ${HEADERS_CPPREST}
  ${HEADERS_PPLX}
  ${HEADERS_DETAILS}
  pch/stdafx.h
  http/client/http_client.cpp
  http/client/http_client_impl.h
  http/client/http_client_msg.cpp
  http/common/connection_pool_helpers.h
  http/common/http_compression.cpp
  http/common/http_helpers.cpp
  http/common/http_msg.cpp
  http/common/internal_http_helpers.h
  http/oauth/oauth2.cpp
  json/json.cpp
  json/json_parsing.cpp
  json/json_serialization.cpp
  uri/uri.cpp
  uri/uri_builder.cpp
  utilities/asyncrt_utils.cpp
  utilities/base64.cpp
  utilities/web_utilities.cpp
)

add_library(cpprest ${SOURCES})
target_include_directories(cpprest
  PUBLIC
    $<INSTALL_INTERFACE:include> $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
  PRIVATE
    pch
)

## Sub-components
# Websockets component
target_compile_definitions(cpprest PUBLIC -DCPPREST_EXCLUDE_WEBSOCKETS=1)

# Compression component
if(CPPREST_EXCLUDE_COMPRESSION)
  if(NOT CPPREST_EXCLUDE_BROTLI)
    message(FATAL_ERROR "Use of Brotli requires compression to be enabled")
  endif()
  target_compile_definitions(cpprest PRIVATE -DCPPREST_EXCLUDE_COMPRESSION=1)
else()
  cpprest_find_zlib()
  target_link_libraries(cpprest PRIVATE cpprestsdk_zlib_internal)
  if(CPPREST_EXCLUDE_BROTLI)
    target_compile_definitions(cpprest PRIVATE -DCPPREST_EXCLUDE_BROTLI=1)
  else()
    cpprest_find_brotli()
  endif()
endif()

# PPLX component
if(CPPREST_PPLX_IMPL STREQUAL "apple")
  find_library(COREFOUNDATION CoreFoundation "/")
  find_library(SECURITY Security "/")
  target_link_libraries(cpprest PUBLIC ${COREFOUNDATION} ${SECURITY})
  target_sources(cpprest PRIVATE pplx/pplxapple.cpp pplx/pplx.cpp pplx/threadpool.cpp ../include/pplx/threadpool.h)
  if(CPPREST_INSTALL_HEADERS)
    install(FILES ../include/pplx/threadpool.h DESTINATION include/pplx)
  endif()
elseif(CPPREST_PPLX_IMPL STREQUAL "linux")
  target_sources(cpprest PRIVATE pplx/pplxlinux.cpp pplx/pplx.cpp pplx/threadpool.cpp ../include/pplx/threadpool.h)
  if(CPPREST_INSTALL_HEADERS)
    install(FILES ../include/pplx/threadpool.h DESTINATION include/pplx)
  endif()
elseif(CPPREST_PPLX_IMPL STREQUAL "win")
  target_sources(cpprest PRIVATE pplx/pplxwin.cpp)
  if(CPPREST_WEBSOCKETS_IMPL STREQUAL "wspp" OR CPPREST_HTTP_CLIENT_IMPL STREQUAL "asio")
    target_sources(cpprest PRIVATE pplx/threadpool.cpp ../include/pplx/threadpool.h)
    if(CPPREST_INSTALL_HEADERS)
      install(FILES ../include/pplx/threadpool.h DESTINATION include/pplx)
    endif()
  endif()
elseif(CPPREST_PPLX_IMPL STREQUAL "winpplx")
  target_compile_definitions(cpprest PUBLIC -DCPPREST_FORCE_PPLX=1)
  target_sources(cpprest PRIVATE pplx/pplxwin.cpp pplx/pplx.cpp pplx/threadpool.cpp ../include/pplx/threadpool.h)
  if(CPPREST_INSTALL_HEADERS)
    install(FILES ../include/pplx/threadpool.h DESTINATION include/pplx)
  endif()
elseif(CPPREST_PPLX_IMPL STREQUAL "winrt")
  target_sources(cpprest PRIVATE pplx/pplxwin.cpp)
else()
  message(FATAL_ERROR "Invalid implementation")
endif()

# Http client component
cpprest_find_boost()
cpprest_find_botan()
target_sources(cpprest PRIVATE http/client/http_client_asio.cpp http/client/x509_cert_utilities.cpp)
target_link_libraries(cpprest PUBLIC cpprestsdk_boost_internal cpprestsdk_botan_internal)

# fileio streams component
if(CPPREST_FILEIO_IMPL STREQUAL "win32")
  target_sources(cpprest PRIVATE streams/fileio_win32.cpp)
elseif(CPPREST_FILEIO_IMPL STREQUAL "winrt")
  target_sources(cpprest PRIVATE streams/fileio_winrt.cpp)
elseif(CPPREST_FILEIO_IMPL STREQUAL "posix")
  target_sources(cpprest PRIVATE streams/fileio_posix.cpp)
else()
  message(FATAL_ERROR "Invalid implementation")
endif()

# http listener component
# NONE

if(MSVC)
  get_target_property(_srcs cpprest SOURCES)

  if(NOT CMAKE_GENERATOR MATCHES "Visual Studio .*")
    set_property(SOURCE pch/stdafx.cpp APPEND PROPERTY OBJECT_OUTPUTS "${CMAKE_CURRENT_BINARY_DIR}/stdafx.pch")
    set_property(SOURCE ${_srcs} APPEND PROPERTY OBJECT_DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/stdafx.pch")
  endif()

  set_source_files_properties(pch/stdafx.cpp PROPERTIES COMPILE_FLAGS "/Ycstdafx.h /Zm120")
  target_sources(cpprest PRIVATE pch/stdafx.cpp)
  target_compile_options(cpprest PRIVATE /Yustdafx.h /Zm120)
endif()

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang" OR CMAKE_CXX_COMPILER_ID MATCHES "GNU")
  if(WERROR)
    target_compile_options(cpprest PRIVATE -Werror)
  endif()
  target_compile_options(cpprest PRIVATE -pedantic ${WARNINGS})
elseif(CMAKE_CXX_COMPILER_ID MATCHES "MSVC")
  if(WERROR)
    target_compile_options(cpprest PRIVATE /WX ${WARNINGS})
  endif()
else()
  message(FATAL_ERROR "Unknown compiler")
endif()

if(WIN32)
  if (BUILD_SHARED_LIBS)
    target_compile_definitions(cpprest PRIVATE -D_ASYNCRT_EXPORT -D_PPLX_EXPORT -D_USRDLL)
  else()
    target_compile_definitions(cpprest PUBLIC -D_NO_ASYNCRTIMP -D_NO_PPLXIMP)
  endif()
elseif(ANDROID)
  target_link_libraries(cpprest PRIVATE ${ANDROID_STL_FLAGS})
endif()

if (WIN32 AND NOT WINDOWS_STORE AND NOT WINDOWS_PHONE)
  target_link_libraries(cpprest PRIVATE
    bcrypt.lib
    crypt32.lib
  )
elseif(WINDOWS_STORE)
  if(NOT CMAKE_GENERATOR MATCHES "Visual Studio .*")
    target_compile_definitions(cpprest PRIVATE -DWINAPI_FAMILY=WINAPI_FAMILY_PC_APP)
    get_target_property(LINK_FLAGS cpprest LINK_FLAGS)
    if(NOT LINK_FLAGS)
        set(LINK_FLAGS "")
    endif()
    set(LINK_FLAGS "${LINK_FLAGS} /APPCONTAINER")
    set_target_properties(cpprest PROPERTIES LINK_FLAGS "${LINK_FLAGS}")
  endif()
endif()

set_target_properties(cpprest PROPERTIES OUTPUT_NAME "cpprest${CPPREST_ABI_TAG}")
if(WIN32)
elseif(ANDROID)
  # Do not use SOVERSION on android. It is completely unsupported (and causes problems).
  # Perhaps revisit in the future? (NDK r9d, 8/7/14)
else()
  set_target_properties(cpprest PROPERTIES
    SOVERSION ${CPPREST_VERSION_MAJOR}.${CPPREST_VERSION_MINOR})
endif()

if(CPPREST_INSTALL_HEADERS)
  install(FILES ${HEADERS_CPPREST} DESTINATION include/cpprest)
  install(FILES ${HEADERS_PPLX} DESTINATION include/pplx)
  install(FILES ${HEADERS_DETAILS} DESTINATION include/cpprest/details)
endif()

if(CPPREST_INSTALL)
  set(CPPREST_USES_BOOST OFF)
  set(CPPREST_USES_ZLIB OFF)
  set(CPPREST_USES_BROTLI OFF)
  set(CPPREST_USES_BOTAN OFF)

  set(CPPREST_TARGETS cpprest)
  if(TARGET cpprestsdk_boost_internal)
    list(APPEND CPPREST_TARGETS cpprestsdk_boost_internal)
    set(CPPREST_USES_BOOST ON)
  endif()
  if(TARGET cpprestsdk_zlib_internal)
    list(APPEND CPPREST_TARGETS cpprestsdk_zlib_internal)
    set(CPPREST_USES_ZLIB ON)
  endif()
  if(TARGET cpprestsdk_brotli_internal)
    list(APPEND CPPREST_TARGETS cpprestsdk_brotli_internal)
    set(CPPREST_USES_BROTLI ON)
  endif()
  if(TARGET cpprestsdk_botan_internal)
    list(APPEND CPPREST_TARGETS cpprestsdk_botan_internal)
    set(CPPREST_USES_BOTAN ON)
  endif()
  if(TARGET cpprestsdk_websocketpp_internal)
    list(APPEND CPPREST_TARGETS cpprestsdk_websocketpp_internal)
  endif()
  install(
    TARGETS ${CPPREST_TARGETS}
    EXPORT cpprestsdk-targets
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  )

  configure_file(../cmake/cpprestsdk-config.in.cmake "${CMAKE_CURRENT_BINARY_DIR}/cpprestsdk-config.cmake" @ONLY)

  install(
    FILES "${CMAKE_CURRENT_BINARY_DIR}/cpprestsdk-config.cmake"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/${CPPREST_EXPORT_DIR}
  )
  install(
    EXPORT cpprestsdk-targets
    FILE cpprestsdk-targets.cmake
    NAMESPACE cpprestsdk::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/${CPPREST_EXPORT_DIR}
  )
endif()
