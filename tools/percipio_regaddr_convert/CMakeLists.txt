add_library(percipio_regaddr STATIC
    src/regaddr_convert.cpp
)
target_include_directories(percipio_regaddr
    PUBLIC "${PROJECT_SOURCE_DIR}/core/include"
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

# The checked-in header keeps normal builds Python/CSV independent. If it is
# missing, the custom command below generates it before compiling the library.
set(PERCIPIO_REGADDR_CSV_DIR "${PROJECT_SOURCE_DIR}/../../TYGenICamReg/csv")
set(PERCIPIO_REGADDR_CSV_HEADER "${CMAKE_CURRENT_SOURCE_DIR}/src/regaddr_csv_data.h")
find_package(Python3 QUIET COMPONENTS Interpreter)
if(Python3_Interpreter_FOUND AND EXISTS "${PERCIPIO_REGADDR_CSV_DIR}")
    file(GLOB_RECURSE PERCIPIO_REGADDR_CSV_FILES
        CONFIGURE_DEPENDS
        "${PERCIPIO_REGADDR_CSV_DIR}/*.csv"
    )
    add_custom_command(
        OUTPUT "${PERCIPIO_REGADDR_CSV_HEADER}"
        COMMAND
            "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/gen_regaddr_csv_data.py"
            --csv-dir "${PERCIPIO_REGADDR_CSV_DIR}"
            --output "${PERCIPIO_REGADDR_CSV_HEADER}"
        DEPENDS
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/gen_regaddr_csv_data.py"
            ${PERCIPIO_REGADDR_CSV_FILES}
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        COMMENT "Generating embedded GenICam register CSV data"
        VERBATIM
    )
    add_custom_target(regenerate_regaddr_csv
        DEPENDS "${PERCIPIO_REGADDR_CSV_HEADER}"
    )
    add_dependencies(percipio_regaddr regenerate_regaddr_csv)
elseif(NOT EXISTS "${PERCIPIO_REGADDR_CSV_HEADER}")
    message(FATAL_ERROR
        "Cannot generate ${PERCIPIO_REGADDR_CSV_HEADER}: "
        "Python3 and TYGenICamReg/csv are required"
    )
endif()

gm86x_enable_warnings(percipio_regaddr)

add_executable(regaddr_convert
    test/regaddr_convert_main.cpp
)
target_include_directories(regaddr_convert PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(regaddr_convert PRIVATE percipio_regaddr)
gm86x_enable_warnings(regaddr_convert)
