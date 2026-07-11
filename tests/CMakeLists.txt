# Pure-logic unit tests. Depends only on the C++ standard library, so this
# target builds without conan. Sources are added as units are extracted.
add_executable(pkgj_tests
  test_main.cpp
  test_smoke.cpp
  test_systems.cpp
  test_jsonscan.cpp
  ${CMAKE_SOURCE_DIR}/src/systems.cpp
  ${CMAKE_SOURCE_DIR}/src/jsonscan.cpp
)
target_include_directories(pkgj_tests PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_SOURCE_DIR}/src
)
target_compile_features(pkgj_tests PRIVATE cxx_std_17)
enable_testing()
add_test(NAME pkgj_tests COMMAND pkgj_tests)
