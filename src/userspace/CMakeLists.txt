set(SANITY_TEST_NAME "sanity-check")

add_executable(${SANITY_TEST_NAME})

target_sources(${SANITY_TEST_NAME} PRIVATE sanity-check.c)

target_include_directories(${SANITY_TEST_NAME} PUBLIC ${PROJECT_SOURCE_DIR}/include)
target_compile_definitions(${SANITY_TEST_NAME} PUBLIC USERSPACE_APP)

set(DMA_TEST_NAME "dma-check")

add_executable(${DMA_TEST_NAME})

target_sources(${DMA_TEST_NAME} PRIVATE dma-kernel-module-test.c)

target_include_directories(${DMA_TEST_NAME} PUBLIC ${PROJECT_SOURCE_DIR}/include)
target_compile_definitions(${DMA_TEST_NAME} PUBLIC USERSPACE_APP)
