include_guard()

# ############################### Getting sub-directories from given path ###########################################

macro(
    get_subdirectories
    path
    subdirectories)

    file(
        GLOB
        children
        RELATIVE ${path}
        ${path}/*)

    set(dirlist
        "")

    foreach(
        child
        ${children})

        if(IS_DIRECTORY
           ${path}/${child})

            if(NOT ${child} STREQUAL "CMakeFiles")
                list(
                    APPEND
                    dirlist
                    ${child})
            endif()

        endif()

    endforeach()

    set(${subdirectories}
        ${dirlist})

endmacro()
