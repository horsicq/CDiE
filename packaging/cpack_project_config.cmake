# Read by cpack once per generator (CPACK_PROJECT_CONFIG_FILE in the top-level
# CMakeLists.txt); CPACK_GENERATOR is the generator about to run.
#
# A .deb installs below /usr, so it needs the FHS install layout. Made from a
# PORTABLE tree it would put the programs, db/ and README.md straight into
# /usr/bin and /usr.

if(CPACK_GENERATOR STREQUAL "DEB")
    if(NOT CPACK_CDIE_INSTALL_LAYOUT STREQUAL "FHS")
        message(FATAL_ERROR
            "cdie: a .deb needs a build configured with -DCDIE_INSTALL_LAYOUT=FHS "
            "(this one is '${CPACK_CDIE_INSTALL_LAYOUT}'). Use packaging/debian/build_deb.sh.")
    endif()
    # The control file's synopsis line: the package has the GUI and the
    # updater too, not only the console engine the summary names.
    set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "${CPACK_CDIE_DEBIAN_SUMMARY}")
endif()
