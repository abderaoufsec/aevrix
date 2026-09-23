# Sanitizer configuration for Aevrix
# This file provides functions to enable various sanitizers

function(enable_sanitizer target sanitizer)
    if(NOT sanitizer)
        return()
    endif()

    if(MSVC)
        message(WARNING "Sanitizers are not fully supported on MSVC")
        return()
    endif()

    if("${sanitizer}" STREQUAL "address")
        target_compile_options(${target} PRIVATE
            -fsanitize=address
            -fno-omit-frame-pointer
            -g
        )
        target_link_options(${target} PRIVATE
            -fsanitize=address
        )
    elseif("${sanitizer}" STREQUAL "undefined")
        target_compile_options(${target} PRIVATE
            -fsanitize=undefined
            -fno-sanitize-recover=all
            -g
        )
        target_link_options(${target} PRIVATE
            -fsanitize=undefined
        )
    elseif("${sanitizer}" STREQUAL "thread")
        target_compile_options(${target} PRIVATE
            -fsanitize=thread
            -g
        )
        target_link_options(${target} PRIVATE
            -fsanitize=thread
        )
    else()
        message(WARNING "Unknown sanitizer: ${sanitizer}")
    endif()
endfunction()

function(enable_all_sanitizers target)
    if(MSVC)
        message(WARNING "Sanitizers are not fully supported on MSVC")
        return()
    endif()

    target_compile_options(${target} PRIVATE
        -fsanitize=address,undefined
        -fno-sanitize-recover=all
        -fno-omit-frame-pointer
        -g
    )
    target_link_options(${target} PRIVATE
        -fsanitize=address,undefined
    )
endfunction()
