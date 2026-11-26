# SPDX-FileCopyrightText: 2026 Vector Informatik GmbH
# SPDX-License-Identifier: MIT
#
# CPack pre-build script (CPACK_PRE_BUILD_SCRIPTS, see cmake/Packaging.cmake).
#
# The install rules put the documentation under <docdir>, which is where the
# .deb needs it (/usr/share/doc/sil-kit-android-bridge). A zip has no such
# convention, so for ZIP this moves the contents of <docdir> to the top of the
# archive, next to bin/:
#
#   bin/  docs/  README.md  LICENSE.md  THIRD_PARTY_NOTICES.md
#
# CPack runs it once per generator, after staging the install tree and before
# collecting the files to package. CPACK_SKAB_DOC_ROOT is <docdir> relative to
# the staging root.

if(NOT CPACK_GENERATOR STREQUAL "ZIP")
    return()
endif()

set(_stage "${CPACK_TEMPORARY_DIRECTORY}")
if(NOT _stage OR NOT IS_DIRECTORY "${_stage}")
    message(FATAL_ERROR "ZipLayout: CPACK_TEMPORARY_DIRECTORY ('${_stage}') is not a directory")
endif()
if(NOT CPACK_SKAB_DOC_ROOT)
    message(FATAL_ERROR "ZipLayout: CPACK_SKAB_DOC_ROOT is not set")
endif()

set(_docroot "${_stage}/${CPACK_SKAB_DOC_ROOT}")
if(NOT IS_DIRECTORY "${_docroot}")
    message(FATAL_ERROR "ZipLayout: ${CPACK_SKAB_DOC_ROOT} was not staged")
endif()

file(GLOB _entries LIST_DIRECTORIES true "${_docroot}/*")
foreach(_entry IN LISTS _entries)
    get_filename_component(_name "${_entry}" NAME)
    if(EXISTS "${_stage}/${_name}")
        message(FATAL_ERROR "ZipLayout: '${_name}' would overwrite an entry at the top of the archive")
    endif()
    file(RENAME "${_entry}" "${_stage}/${_name}")
endforeach()

# Remove <docdir> and its parents (share/doc, share) while they are empty, so
# anything else installed under share/ stays where it is.
set(_relative "${CPACK_SKAB_DOC_ROOT}")
while(_relative)
    file(GLOB _left LIST_DIRECTORIES true "${_stage}/${_relative}/*")
    if(_left)
        break()
    endif()
    file(REMOVE_RECURSE "${_stage}/${_relative}")
    get_filename_component(_relative "${_relative}" DIRECTORY)
endwhile()
