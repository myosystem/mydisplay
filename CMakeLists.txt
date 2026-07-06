file(GLOB SRCS *.cpp)

add_executable(mydisplay ${SRCS})

target_link_libraries(mydisplay PRIVATE mylibc)

target_link_options(mydisplay PRIVATE
    -nostdlib
    -T ${CMAKE_CURRENT_SOURCE_DIR}/linker.ld
)

set_target_properties(mydisplay PROPERTIES
    LINK_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/linker.ld
)

add_custom_command(TARGET mydisplay POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory ${BOOT_OUT_DIR}
    COMMAND objcopy -O binary
        --only-section=.text --only-section=.rodata --only-section=.data
        $<TARGET_FILE:mydisplay>
        ${BOOT_OUT_DIR}/display.o
    VERBATIM
)
