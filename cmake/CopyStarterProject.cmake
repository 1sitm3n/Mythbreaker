# The build output is also a usable workspace. Never overwrite a scene a user
# has saved in the starter project just because the executable was relinked.
if(NOT EXISTS "${DESTINATION}/Mythbreaker.project.json")
    if(EXISTS "${DESTINATION}")
        file(GLOB EXISTING_CONTENT "${DESTINATION}/*")
        if(EXISTING_CONTENT)
            message(STATUS "Keeping existing project directory: ${DESTINATION}")
            return()
        endif()
    endif()
    file(MAKE_DIRECTORY "${DESTINATION}")
    file(COPY "${SOURCE}/" DESTINATION "${DESTINATION}")
endif()
