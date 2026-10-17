cmake_minimum_required(VERSION 3.16)

# This small rendering suite can also run without configuring the full game or GTest.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(CardVisualEffectsTests LANGUAGES CXX)
  enable_testing()
endif()

find_package(Qt6 REQUIRED COMPONENTS Widgets)
add_executable(card_visual_effects_test card_visual_effects_test.cpp)
target_compile_features(card_visual_effects_test PRIVATE cxx_std_20)
target_link_libraries(card_visual_effects_test PRIVATE Qt6::Widgets)
add_test(NAME card_visual_effects COMMAND card_visual_effects_test)
set_tests_properties(card_visual_effects PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 15 LABELS ci)

add_executable(card_draw_animation_test card_draw_animation_test.cpp)
target_compile_features(card_draw_animation_test PRIVATE cxx_std_20)
target_link_libraries(card_draw_animation_test PRIVATE Qt6::Widgets)
add_test(NAME card_draw_animation COMMAND card_draw_animation_test)
set_tests_properties(card_draw_animation PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 15 LABELS ci)
