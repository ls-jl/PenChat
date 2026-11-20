cmake_minimum_required(VERSION 3.16)
project(EdgeSearch VERSION 0.14.0 LANGUAGES CXX)

include(GNUInstallDirs)
option(EDGE_SEARCH_BUILD_TESTS "Build host tests" OFF)
option(EDGE_SEARCH_SANITIZE "Enable address and undefined behavior sanitizers" OFF)
option(EDGE_SEARCH_USE_IMPERSONATE "Use native browser TLS/HTTP profiles" OFF)
set(EDGE_SEARCH_IMPERSONATE_ROOT "" CACHE PATH "Installed libcurl-impersonate prefix")
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
if(EDGE_SEARCH_USE_IMPERSONATE)
    find_package(CurlImpersonate REQUIRED)
    set(EDGE_SEARCH_CURL_TARGET CurlImpersonate::CurlImpersonate)
else()
    find_package(CURL 7.68 REQUIRED)
    set(EDGE_SEARCH_CURL_TARGET CURL::libcurl)
endif()
find_package(LibXml2 2.9 REQUIRED)

add_library(edgesearch src/engines.cpp src/parser.cpp src/search.cpp src/transport.cpp src/url.cpp src/json.cpp src/snapshot.cpp src/cooldown.cpp src/resolver.cpp src/trace.cpp src/browser.cpp src/quality.cpp src/repository.cpp src/rss.cpp src/query_constraints.cpp src/image.cpp src/browser_network.cpp)
add_library(EdgeSearch::edgesearch ALIAS edgesearch)
target_compile_features(edgesearch PUBLIC cxx_std_17)
target_include_directories(edgesearch PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    PRIVATE src)
target_link_libraries(edgesearch PRIVATE ${EDGE_SEARCH_CURL_TARGET} LibXml2::LibXml2)
# Installed shared consumers must also locate private runtime dependencies when
# they live outside the system loader paths. LINK_ONLY keeps their headers from
# changing the chatbot's own curl compilation.
get_target_property(_edge_library_type edgesearch TYPE)
if(_edge_library_type STREQUAL "SHARED_LIBRARY")
    target_link_libraries(edgesearch INTERFACE
        "$<LINK_ONLY:${EDGE_SEARCH_CURL_TARGET}>" "$<LINK_ONLY:LibXml2::LibXml2>")
endif()
target_compile_definitions(edgesearch PRIVATE EDGE_SEARCH_USE_IMPERSONATE=$<BOOL:${EDGE_SEARCH_USE_IMPERSONATE}>)
target_compile_options(edgesearch PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
set_target_properties(edgesearch PROPERTIES POSITION_INDEPENDENT_CODE ON
    VERSION ${PROJECT_VERSION} SOVERSION "${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}")

add_executable(edge-search src/main.cpp)
target_link_libraries(edge-search PRIVATE edgesearch)
target_compile_definitions(edge-search PRIVATE EDGE_SEARCH_VERSION="${PROJECT_VERSION}")
target_compile_options(edge-search PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)

add_executable(edge-browser src/browser_main.cpp)
target_link_libraries(edge-browser PRIVATE edgesearch)
target_compile_definitions(edge-browser PRIVATE EDGE_SEARCH_VERSION="${PROJECT_VERSION}")
target_compile_options(edge-browser PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)

if(EDGE_SEARCH_SANITIZE)
    foreach(target edgesearch edge-search edge-browser)
        target_compile_options(${target} PUBLIC -fsanitize=address,undefined -fno-omit-frame-pointer)
        target_link_options(${target} PUBLIC -fsanitize=address,undefined)
    endforeach()
endif()

install(TARGETS edgesearch edge-search edge-browser EXPORT EdgeSearchTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(EXPORT EdgeSearchTargets NAMESPACE EdgeSearch:: DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/EdgeSearch)
include(CMakePackageConfigHelpers)
configure_package_config_file(cmake/EdgeSearchConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/EdgeSearchConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/EdgeSearch)
write_basic_package_version_file(${CMAKE_CURRENT_BINARY_DIR}/EdgeSearchConfigVersion.cmake
    VERSION ${PROJECT_VERSION} COMPATIBILITY SameMinorVersion)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/EdgeSearchConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/EdgeSearchConfigVersion.cmake DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/EdgeSearch)
install(FILES cmake/FindCurlImpersonate.cmake DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/EdgeSearch)
install(FILES LICENSE THIRD_PARTY_NOTICES.txt DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/edge-search)
install(DIRECTORY licenses/ DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/edge-search)

if(EDGE_SEARCH_BUILD_TESTS)
    enable_testing()
    add_executable(edge-search-unit tests/unit.cpp)
    target_include_directories(edge-search-unit PRIVATE src)
    target_link_libraries(edge-search-unit PRIVATE edgesearch)
    add_test(NAME unit COMMAND edge-search-unit)
    add_executable(edge-issue-fixes tests/fixes_unit.cpp)
    target_include_directories(edge-issue-fixes PRIVATE src)
    target_link_libraries(edge-issue-fixes PRIVATE edgesearch)
    add_test(NAME issue_fixes COMMAND edge-issue-fixes ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/sqlite-wal-struck.html)
    add_executable(edge-engine-upgrade tests/engine_upgrade.cpp)
    target_include_directories(edge-engine-upgrade PRIVATE src)
    target_link_libraries(edge-engine-upgrade PRIVATE edgesearch)
    add_test(NAME engine_upgrade COMMAND edge-engine-upgrade ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/bing-rss-musl.xml ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/bing-rss-wrong-br2.xml)
    add_executable(edge-quality-workflow tests/quality_workflow_check.cpp)
    target_include_directories(edge-quality-workflow PRIVATE src)
    target_link_libraries(edge-quality-workflow PRIVATE edgesearch)
    add_executable(edge-upgrade-unit tests/upgrade_unit.cpp)
    target_include_directories(edge-upgrade-unit PRIVATE src)
    target_link_libraries(edge-upgrade-unit PRIVATE edgesearch LibXml2::LibXml2)
    add_test(NAME upgrade_unit COMMAND edge-upgrade-unit ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/openwrt-25.12.6-sha256sums.txt ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/wrong-entity-bing.html ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/script-navigation.html)
    add_executable(edge-browser-unit tests/browser_unit.cpp)
    target_link_libraries(edge-browser-unit PRIVATE edgesearch)
    add_test(NAME browser_unit COMMAND edge-browser-unit)
    add_executable(edge-image-unit tests/images_unit.cpp)
    target_include_directories(edge-image-unit PRIVATE src)
    target_link_libraries(edge-image-unit PRIVATE edgesearch)
    add_test(NAME image_unit COMMAND edge-image-unit)
    add_executable(edge-readiness-unit tests/readiness_unit.cpp)
    target_include_directories(edge-readiness-unit PRIVATE src)
    target_link_libraries(edge-readiness-unit PRIVATE edgesearch)
    add_test(NAME readiness_unit COMMAND edge-readiness-unit)
    add_executable(edge-browser-recovery-check tests/browser_recovery_check.cpp)
    target_include_directories(edge-browser-recovery-check PRIVATE src)
    target_link_libraries(edge-browser-recovery-check PRIVATE edgesearch)
    add_executable(edge-search-network-probe tests/network_probe.cpp)
    target_include_directories(edge-search-network-probe PRIVATE src)
    target_link_libraries(edge-search-network-probe PRIVATE edgesearch)
    add_executable(edge-search-browser-flow-probe tests/browser_flow_probe.cpp)
    target_include_directories(edge-search-browser-flow-probe PRIVATE src)
    target_link_libraries(edge-search-browser-flow-probe PRIVATE edgesearch)
    add_executable(edge-search-connection-probe tests/connection_probe.cpp)
    target_include_directories(edge-search-connection-probe PRIVATE src)
    target_link_libraries(edge-search-connection-probe PRIVATE edgesearch)
    add_executable(edge-search-resource-check tests/resource_check.cpp)
    target_include_directories(edge-search-resource-check PRIVATE src)
    target_link_libraries(edge-search-resource-check PRIVATE edgesearch)
    add_executable(edge-search-soak-check tests/soak_check.cpp)
    target_link_libraries(edge-search-soak-check PRIVATE edgesearch)
    add_executable(edge-search-stdin-benchmark tests/stdin_benchmark_check.cpp)
    target_compile_features(edge-search-stdin-benchmark PRIVATE cxx_std_17)
    foreach(check transport session workflow tls doh idle)
        add_executable(edge-search-${check}-check tests/${check}_check.cpp)
        target_include_directories(edge-search-${check}-check PRIVATE src)
        target_link_libraries(edge-search-${check}-check PRIVATE edgesearch)
    endforeach()
    find_package(Python3 COMPONENTS Interpreter QUIET)
    if(EDGE_SEARCH_USE_IMPERSONATE)
        find_package(CURL 7.68 QUIET)
        if(CURL_FOUND)
            foreach(order stock-first edge-first)
                add_executable(edge-search-mixed-${order} tests/mixed_curl_check.cpp)
                if(order STREQUAL "stock-first")
                    target_link_libraries(edge-search-mixed-${order} PRIVATE CURL::libcurl edgesearch)
                else()
                    target_link_libraries(edge-search-mixed-${order} PRIVATE edgesearch CURL::libcurl)
                endif()
            endforeach()
        endif()
    endif()
    if(Python3_Interpreter_FOUND AND NOT CMAKE_CROSSCOMPILING)
        add_test(NAME images_http COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/images_integration.py $<TARGET_FILE:edge-browser>)
        set_tests_properties(images_http PROPERTIES TIMEOUT 30)
        add_test(NAME issue_fixes_http COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/issue_fixes_integration.py $<TARGET_FILE:edge-browser>)
        set_tests_properties(issue_fixes_http PROPERTIES TIMEOUT 20)
        add_test(NAME engine_slice_http COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/engine_slice_integration.py $<TARGET_FILE:edge-engine-upgrade>)
        set_tests_properties(engine_slice_http PROPERTIES TIMEOUT 10)
        add_test(NAME quality_workflow_http COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/quality_workflow_integration.py $<TARGET_FILE:edge-quality-workflow>)
        set_tests_properties(quality_workflow_http PROPERTIES TIMEOUT 15)
        add_test(NAME full_browser_http COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/full_browser_integration.py $<TARGET_FILE:edge-browser>)
        set_tests_properties(full_browser_http PROPERTIES TIMEOUT 45)
        add_test(NAME browser_http COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/browser_integration.py $<TARGET_FILE:edge-browser>)
        set_tests_properties(browser_http PROPERTIES TIMEOUT 45)
        add_test(NAME parser_structure COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/parser_structure.py $<TARGET_FILE:edge-search>)
        set_tests_properties(parser_structure PROPERTIES TIMEOUT 60)
        add_test(NAME stdin_cli COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/stdin_integration.py $<TARGET_FILE:edge-search>)
        set_tests_properties(stdin_cli PROPERTIES TIMEOUT 20)
        add_test(NAME local_http COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration.py
            $<TARGET_FILE:edge-search-transport-check> $<TARGET_FILE:edge-search>)
        set_tests_properties(local_http PROPERTIES TIMEOUT 30)
        add_test(NAME ad_accuracy COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/ad_accuracy.py $<TARGET_FILE:edge-search>)
        add_test(NAME session_http COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/session_integration.py $<TARGET_FILE:edge-search-session-check>)
        set_tests_properties(session_http PROPERTIES TIMEOUT 30)
        add_test(NAME workflow_http COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/workflow_integration.py $<TARGET_FILE:edge-search-workflow-check>)
        set_tests_properties(workflow_http PROPERTIES TIMEOUT 15)
        find_program(EDGE_SEARCH_OPENSSL_EXECUTABLE openssl)
        if(EDGE_SEARCH_OPENSSL_EXECUTABLE)
            add_test(NAME browser_recovery_http COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/tests/browser_recovery_integration.py $<TARGET_FILE:edge-browser-recovery-check>)
            set_tests_properties(browser_recovery_http PROPERTIES TIMEOUT 30)
            if(TARGET edge-search-mixed-stock-first)
                add_test(NAME mixed_curl COMMAND ${Python3_EXECUTABLE}
                    ${CMAKE_CURRENT_SOURCE_DIR}/tests/mixed_curl_integration.py
                    $<TARGET_FILE:edge-search-mixed-stock-first> $<TARGET_FILE:edge-search-mixed-edge-first>)
                set_tests_properties(mixed_curl PROPERTIES TIMEOUT 45)
            endif()
            add_test(NAME tls_http COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/tests/tls_integration.py $<TARGET_FILE:edge-search-tls-check>)
            set_tests_properties(tls_http PROPERTIES TIMEOUT 30)
            add_test(NAME doh_http COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/tests/doh_integration.py $<TARGET_FILE:edge-search-doh-check> $<TARGET_FILE:edge-search>)
            set_tests_properties(doh_http PROPERTIES TIMEOUT 30)
            add_test(NAME connection_http COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/tests/connection_integration.py $<TARGET_FILE:edge-search-connection-probe>)
            set_tests_properties(connection_http PROPERTIES TIMEOUT 30)
            add_test(NAME idle_http COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/tests/idle_integration.py $<TARGET_FILE:edge-search-idle-check> $<TARGET_FILE:edge-search>)
            set_tests_properties(idle_http PROPERTIES TIMEOUT 30)
        endif()
    endif()
endif()
