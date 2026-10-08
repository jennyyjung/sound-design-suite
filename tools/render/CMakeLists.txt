add_executable (SoundSuiteRender)
set_target_properties (SoundSuiteRender PROPERTIES OUTPUT_NAME soundsuite-render)

target_sources (SoundSuiteRender PRIVATE Main.cpp)

target_link_libraries (SoundSuiteRender PRIVATE SoundSuite soundsuite_core soundsuite_tuning)
target_include_directories (SoundSuiteRender PRIVATE $<TARGET_PROPERTY:SoundSuite,INCLUDE_DIRECTORIES>)
target_compile_definitions (SoundSuiteRender PRIVATE $<TARGET_PROPERTY:SoundSuite,COMPILE_DEFINITIONS>)
