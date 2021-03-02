vcpkg_find_acquire_program(FLEX)

vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO finos/OpenMAMA
    REF c4925ee103add1a51c1d27be45b46d97af347f36 # https://github.com/finos/OpenMAMA/releases/tag/OpenMAMA-6.3.1-release
    SHA512 e2773d082dd28e073fe81223fc113b1a5db7cd0d95e150e9f3f02c8c9483b9219b5d10682a125dd792c3a7877e15b90fd908084a4c89af4ec8d8c0389c282de2
    HEAD_REF next
)

vcpkg_configure_cmake(
    SOURCE_PATH ${SOURCE_PATH}
    PREFER_NINJA
    OPTIONS
        -DPROTON_ROOT=${CURRENT_INSTALLED_DIR}
        -DAPR_ROOT=${CURRENT_INSTALLED_DIR}
        -DINSTALL_RUNTIME_DEPENDENCIES=OFF
        -DFLEX_EXECUTABLE=${FLEX}
        -DWITH_EXAMPLES=OFF
        -DWITH_TESTTOOLS=OFF
)

vcpkg_install_cmake()

# Copy across license files and copyright
file(MAKE_DIRECTORY ${CURRENT_PACKAGES_DIR}/share/${PORT})
file(COPY ${SOURCE_PATH}/LICENSE-3RD-PARTY.txt DESTINATION ${CURRENT_PACKAGES_DIR}/share/${PORT}/)
file(INSTALL ${SOURCE_PATH}/LICENSE.md DESTINATION ${CURRENT_PACKAGES_DIR}/share/${PORT} RENAME copyright)
file(REMOVE ${CURRENT_PACKAGES_DIR}/LICENSE.md ${CURRENT_PACKAGES_DIR}/debug/LICENSE.md)

# Temporary workaround until upstream project puts dll in right place
if(EXISTS "${CURRENT_PACKAGES_DIR}/lib/libmamaplugindqstrategymd.dll")
    file(RENAME ${CURRENT_PACKAGES_DIR}/lib/libmamaplugindqstrategymd.dll ${CURRENT_PACKAGES_DIR}/bin/libmamaplugindqstrategymd.dll)
endif()
if(EXISTS "${CURRENT_PACKAGES_DIR}/debug/lib/libmamaplugindqstrategymd.dll")
    file(RENAME ${CURRENT_PACKAGES_DIR}/debug/lib/libmamaplugindqstrategymd.dll ${CURRENT_PACKAGES_DIR}/debug/bin/libmamaplugindqstrategymd.dll)
endif()

if(VCPKG_LIBRARY_LINKAGE STREQUAL "static")
    file(GLOB TO_REMOVE "${CURRENT_PACKAGES_DIR}/lib/lib*.so" "${CURRENT_PACKAGES_DIR}/debug/lib/lib*.so")
else()
    file(GLOB TO_REMOVE "${CURRENT_PACKAGES_DIR}/lib/lib*.a" "${CURRENT_PACKAGES_DIR}/debug/lib/lib*.a")
endif()
if(TO_REMOVE)
    file(REMOVE ${TO_REMOVE})
endif()

file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/include")

set(FILE_REGEX)
file(GLOB ROOT_INCLUDES "${CURRENT_PACKAGES_DIR}/include/*.h")
foreach(FILE IN LISTS ROOT_INCLUDES)
    get_filename_component(filename "${FILE}" NAME)
    file(RENAME "${FILE}" "${CURRENT_PACKAGES_DIR}/include/wombat/${filename}")
    string(APPEND FILE_REGEX "|${filename}")
endforeach()

file(GLOB_RECURSE ALL_INCLUDES "${CURRENT_PACKAGES_DIR}/include/*")
foreach(FILE IN LISTS ALL_INCLUDES)
    file(READ "${FILE}" _contents)
    string(REGEX REPLACE "#include [<\"](${FILE_REGEX})[>\"]" "#include <wombat/\\1>" _contents "${_contents}")
    file(WRITE "${FILE}" "${_contents}")
endforeach()

vcpkg_copy_pdbs()
