cmake_minimum_required (VERSION 2.8)
project (mrcp-mediatel)

# Set header files (include)
set (MRCP_MEDIATEL_HEADERS
	include/mrcp_mediatel.h
)
source_group ("include" FILES ${MRCP_MEDIATEL_HEADERS})

# Set source files (src)
set (MRCP_MEDIATEL_SOURCES
	src/mrcp_mediatel.cpp
)
source_group ("src" FILES ${MRCP_MEDIATEL_SOURCES})


# Library declaration
add_library (${PROJECT_NAME} OBJECT
	${MRCP_MEDIATEL_HEADERS}
	${MRCP_MEDIATEL_SOURCES}
)
set_target_properties (${PROJECT_NAME} PROPERTIES FOLDER "libs")

# Preprocessor definitions
add_definitions (
	${MRCP_DEFINES}
	${APR_TOOLKIT_DEFINES}
	${APR_DEFINES}
	${APU_DEFINES}
)

# Include directories
include_directories (
	${PROJECT_SOURCE_DIR}/include
	${APR_TOOLKIT_INCLUDE_DIRS}
	${APR_INCLUDE_DIRS}
	${APU_INCLUDE_DIRS}
)
