# ---------------------------------------------------------------------------
# encode_example
# ---------------------------------------------------------------------------
add_executable(encode_example encode_example.c)
target_link_libraries(encode_example PRIVATE rgb565_qoi)
target_compile_features(encode_example PRIVATE c_std_99)

# ---------------------------------------------------------------------------
# decode_example
# ---------------------------------------------------------------------------
add_executable(decode_example decode_example.c)
target_link_libraries(decode_example PRIVATE rgb565_qoi)
target_compile_features(decode_example PRIVATE c_std_99)
