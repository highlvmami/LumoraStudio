add_library(lumora_testmedia STATIC TestMedia.h TestMedia.cpp)
target_link_libraries(lumora_testmedia PUBLIC lumora_core)

function(lumora_add_test name)
    add_executable(${name} ${name}.cpp)
    target_link_libraries(${name} PRIVATE lumora_core lumora_testmedia Qt6::Test)
    add_test(NAME ${name} COMMAND ${name})
    if(WIN32 AND FFMPEG_RUNTIME_DIR)
        set_tests_properties(${name} PROPERTIES
            ENVIRONMENT "PATH=${FFMPEG_RUNTIME_DIR}\;$<TARGET_FILE_DIR:Qt6::Core>\;$ENV{PATH}")
    endif()
endfunction()

lumora_add_test(tst_timeline)
lumora_add_test(tst_export)
lumora_add_test(tst_photo)
lumora_add_test(tst_project)
