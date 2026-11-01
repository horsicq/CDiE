file(MAKE_DIRECTORY "${RESOURCES_DIR}")
if(IS_DIRECTORY "${CDIE_SOURCE_DIR}/translations")
    file(COPY "${CDIE_SOURCE_DIR}/translations" DESTINATION "${RESOURCES_DIR}"
        FILES_MATCHING PATTERN "*.po")
endif()
# Detection descriptions for the result tree's "?" button.
if(INFO_DIR AND NOT INFO_DIR STREQUAL "NONE" AND IS_DIRECTORY "${INFO_DIR}/html")
    file(COPY "${INFO_DIR}/html" DESTINATION "${RESOURCES_DIR}/info" FILES_MATCHING PATTERN "*.html")
endif()
if(TRANSLATIONS_ONLY)
    return()
endif()
file(COPY "${CDIE_SOURCE_DIR}/LICENSE" DESTINATION "${RESOURCES_DIR}")
if(EXISTS "${CDISASM_DIR}/LICENSE")
    foreach(notice LICENSE THIRD_PARTY_NOTICES.md LICENSES)
        if(EXISTS "${CDISASM_DIR}/${notice}")
            file(COPY "${CDISASM_DIR}/${notice}" DESTINATION "${RESOURCES_DIR}/licenses/cdisasm")
        endif()
    endforeach()
endif()
# cdie has no extra database: a db_extra beside db is never bundled. file(COPY)
# never deletes, so a build tree that bundled one before keeps it; package
# from a clean build tree.
if(IS_DIRECTORY "${RESOURCES_DIR}/db_extra")
    message(WARNING "cdie: ${RESOURCES_DIR}/db_extra is left over from an earlier build and is not used; "
                    "package from a clean build tree")
endif()
if(DATABASE_DIR AND NOT DATABASE_DIR STREQUAL "NONE")
    foreach(database db db_custom)
        if(IS_DIRECTORY "${DATABASE_DIR}/${database}")
            file(COPY "${DATABASE_DIR}/${database}" DESTINATION "${RESOURCES_DIR}"
                PATTERN ".vscode" EXCLUDE
                PATTERN "_ai_provenance" EXCLUDE
                PATTERN ".git*" EXCLUDE)
        endif()
    endforeach()
endif()
