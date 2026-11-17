# Deliberately independent of penchat_core and all rendering/media libraries.
add_library(penchat_rescue_core STATIC Rescue.cpp
  "${ROOT}/native/lvgl-app/src/common.cpp" "${ROOT}/native/lvgl-app/src/update_package.cpp"
  "${ROOT}/native/lvgl-app/src/update_transport.cpp")
target_include_directories(penchat_rescue_core PUBLIC "${ROOT}/native/lvgl-app/src" "${CMAKE_CURRENT_SOURCE_DIR}"
  "${DEPS}/json/single_include" "${DEPS}/curl/include" "${DEPS}/include" "${CMAKE_CURRENT_BINARY_DIR}/../generated")
if(CMAKE_CROSSCOMPILING)
  target_include_directories(penchat_rescue_core PRIVATE "${DEPS}/openssl-headers-${PENCHAT_TARGET}/include")
endif()
target_link_libraries(penchat_rescue_core PUBLIC penchat_zip ${CURL_LIB} dl pthread)
add_library(jsapi_penchat_rescue SHARED Bridge.cpp ${SDK_SRC})
target_include_directories(jsapi_penchat_rescue PRIVATE "${SDK}/include" "${CMAKE_BINARY_DIR}/generated")
target_compile_options(jsapi_penchat_rescue PRIVATE -include pthread.h)
target_link_libraries(jsapi_penchat_rescue PRIVATE penchat_rescue_core
  -Wl,-unresolved-symbols=ignore-all "-Wl,--version-script=${CMAKE_CURRENT_SOURCE_DIR}/exports.map")
if(NOT CMAKE_CROSSCOMPILING)
  add_executable(penchat-rescue-tests "${ROOT}/tests/native/RescueTest.cpp")
  target_link_libraries(penchat-rescue-tests PRIVATE penchat_rescue_core)
  target_compile_options(penchat-rescue-tests PRIVATE -UNDEBUG)
  add_test(NAME penchat-rescue COMMAND penchat-rescue-tests)
  add_executable(penchat-rescue-probe "${ROOT}/tests/native/RescueProbe.cpp")
  target_link_libraries(penchat-rescue-probe PRIVATE penchat_rescue_core)
endif()
