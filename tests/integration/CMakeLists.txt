add_executable(veil_integration_handshake
  handshake_integration.cpp
)

target_link_libraries(veil_integration_handshake PRIVATE
  veil_common
  GTest::gtest_main
)

veil_set_warnings(veil_integration_handshake)

include(GoogleTest)
gtest_discover_tests(veil_integration_handshake PROPERTIES ENVIRONMENT "ASAN_OPTIONS=detect_leaks=0")
