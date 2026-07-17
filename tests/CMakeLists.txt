# ---------------------------------------------------------------------------
# test_rgb565_qoi
# ---------------------------------------------------------------------------
add_executable(test_rgb565_qoi test_rgb565_qoi.c)
target_link_libraries(test_rgb565_qoi PRIVATE rgb565_qoi)
target_compile_features(test_rgb565_qoi PRIVATE c_std_99)

add_test(NAME rgb565_qoi COMMAND test_rgb565_qoi)
