# Pass -DFETCHCONTENT_SOURCE_DIR_CATCH2=/path/to/Catch2 to use a local checkout.
FetchContent_Declare (Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG        v3.7.1
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable (Catch2)

add_executable (SoundSuiteTests
    unit/ClockTests.cpp
    unit/ZoneTableTests.cpp
    unit/WidthTests.cpp
    safety/SafetyTests.cpp
    safety/AllocationGuard.cpp
    JuceSetup.cpp)

# Build against the plugin's shared code, with the same includes and defines.
target_link_libraries (SoundSuiteTests PRIVATE SoundSuite soundsuite_core soundsuite_tuning Catch2::Catch2WithMain)
target_include_directories (SoundSuiteTests PRIVATE $<TARGET_PROPERTY:SoundSuite,INCLUDE_DIRECTORIES>)
target_compile_definitions (SoundSuiteTests PRIVATE $<TARGET_PROPERTY:SoundSuite,COMPILE_DEFINITIONS>)

include (${catch2_SOURCE_DIR}/extras/Catch.cmake)
catch_discover_tests (SoundSuiteTests)
