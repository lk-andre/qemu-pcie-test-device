set(SANITY_TEST_NAME "sanity-check")

add_executable(${SANITY_TEST_NAME})

target_sources(${SANITY_TEST_NAME} PRIVATE sanity-check.c)

target_include_directories(${SANITY_TEST_NAME} PUBLIC ${PROJECT_SOURCE_DIR}/include)
