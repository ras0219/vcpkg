include(CMakeFindDependencyMacro)
find_dependency(ZLIB)
find_dependency(libpng CONFIG)
find_dependency(TIFF)
find_dependency(expat CONFIG)

set(wxWidgets_FOUND TRUE)
set(wxWidgets_INCLUDE_DIRS "${CMAKE_CURRENT_LIST_DIR}/../../include")
set(wxWidgets_LIBRARY_DIRS "")
set(wxWidgets_DEFINITIONS "")
set(wxWidgets_DEFINITIONS_DEBUG "")
set(wxWidgets_CXX_FLAGS "")
set(wxWidgets_USE_FILE "${CMAKE_CURRENT_LIST_DIR}/empty.cmake")
set(wxWidgets_LIBRARIES "@wxWidgets_LIBRARIES@;png;expat::expat;ZLIB::ZLIB")
