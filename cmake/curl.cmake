# libcurl: the distro's package on Linux, or built from pinned source (Windows, or -DWG_BUNDLED_CURL=ON).
# The bundled build is static and small: HTTP(S) + FTP only; TLS from Schannel (Windows' own TLS and
# certificate store) or OpenSSL elsewhere. No zlib/brotli: WackGet stores bytes exactly as served.

if(NOT WG_BUNDLED_CURL)
  find_package(CURL 7.85 REQUIRED) # CURLOPT_PROTOCOLS_STR needs 7.85
  return()
endif()

include(FetchContent)

set(BUILD_CURL_EXE OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(BUILD_STATIC_LIBS ON CACHE BOOL "" FORCE)
set(BUILD_LIBCURL_DOCS OFF CACHE BOOL "" FORCE)
set(BUILD_MISC_DOCS OFF CACHE BOOL "" FORCE)
set(ENABLE_CURL_MANUAL OFF CACHE BOOL "" FORCE)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(CURL_DISABLE_INSTALL ON CACHE BOOL "" FORCE)
if(WIN32)
  set(CURL_USE_SCHANNEL ON CACHE BOOL "" FORCE)
  set(CURL_USE_OPENSSL OFF CACHE BOOL "" FORCE)
  set(CURL_WINDOWS_SSPI ON CACHE BOOL "" FORCE) # proxy NTLM/Negotiate logins
else()
  set(CURL_USE_OPENSSL ON CACHE BOOL "" FORCE)
endif()
foreach(off CURL_USE_LIBPSL CURL_USE_LIBSSH2 CURL_USE_LIBSSH USE_LIBIDN2 USE_NGHTTP2 USE_NGTCP2
            CURL_ZLIB CURL_BROTLI CURL_ZSTD USE_WIN32_IDN USE_APPLE_IDN CURL_USE_GSSAPI)
  set(${off} OFF CACHE BOOL "" FORCE)
endforeach()
foreach(proto DICT FILE GOPHER IMAP LDAP LDAPS MQTT POP3 RTSP SMB SMTP TELNET TFTP WEBSOCKETS)
  set(CURL_DISABLE_${proto} ON CACHE BOOL "" FORCE)
endforeach()

# curl 8.23.0, SHA-256 checked against both github.com/curl/curl releases and curl.se/download
FetchContent_Declare(curl
  URL https://github.com/curl/curl/releases/download/curl-8_23_0/curl-8.23.0.tar.xz
  URL_HASH SHA256=8dadd7d2536a314f982ad05a651b713f426bf7d02cc2c76cffb9796f2218e6c5
  DOWNLOAD_EXTRACT_TIMESTAMP ON)
set(BUILD_TESTING_SAVED ${BUILD_TESTING})
set(BUILD_TESTING OFF)
FetchContent_MakeAvailable(curl)
set(BUILD_TESTING ${BUILD_TESTING_SAVED})
