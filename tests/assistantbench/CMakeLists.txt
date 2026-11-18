# The AI assistant's bench (docs/FINETUNE.md): the built-in assistant on tasks with a known outcome
# (finetune/tasks, finetune/notebooks).
#
# brulion_assistantbench: the tasks, a run of one and its check (bench.h), the model on a
# llama-server (server.h) and the training data of a run (training.h). tst_assistantbench plays
# every task's own solution: each task must be one the assistant can do and its checks right.
# brulion-assistantbench: the program that runs them with the model on the device, opened as the
# app opens it, or on a llama-server (a developer's tool, never installed).
qt_add_library(brulion_assistantbench STATIC bench.h bench.cpp server.h server.cpp training.h training.cpp)
target_include_directories(brulion_assistantbench PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(brulion_assistantbench
    PUBLIC Qt6::Core Qt6::Network brulion_core_ai
    PRIVATE brulion_core_extensions brulion_core_index brulion_core_vault brulion_core_text)
brulion_set_warnings(brulion_assistantbench)

brulion_add_test(tst_assistantbench SOURCES tst_assistantbench.cpp LIBS brulion_assistantbench)
target_compile_definitions(tst_assistantbench PRIVATE BRULION_SOURCE_DIR="${PROJECT_SOURCE_DIR}")

qt_add_executable(brulion-assistantbench main.cpp)
target_link_libraries(brulion-assistantbench PRIVATE brulion_assistantbench)
brulion_set_warnings(brulion-assistantbench)
# The model runner's plugin where this build makes it (--plugin-dir names another).
if(TARGET brulion_localai)
    target_compile_definitions(brulion-assistantbench PRIVATE
        BRULION_LOCAL_AI_OUTPUT_DIR="${BRULION_LOCAL_AI_OUTPUT_DIR}")
    add_dependencies(brulion-assistantbench brulion_localai)
endif()

# The generated tasks (finetune/generate.py): done by their own solutions, the same for the same
# seed. Bash and Python 3 (the Nix check has both); not on Windows.
find_program(BRULION_BASH bash)
find_package(Python3 COMPONENTS Interpreter QUIET)
if(NOT WIN32 AND BRULION_BASH AND Python3_Interpreter_FOUND)
    add_test(NAME tst_assistantbench_generated
             COMMAND ${BRULION_BASH} ${CMAKE_CURRENT_SOURCE_DIR}/tst_generated.sh
                     $<TARGET_FILE:brulion-assistantbench> ${Python3_EXECUTABLE}
                     ${PROJECT_SOURCE_DIR}/finetune/generate.py)
endif()
