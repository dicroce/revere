
add_executable(
    r_crypto_ut
    include/test_r_crypto.h
    include/ed25519_vectors.h
    source/test_r_crypto.cpp
)

target_include_directories(
    r_crypto_ut PUBLIC
    include
)

target_link_libraries(
    r_crypto_ut LINK_PUBLIC
    experimentum
    r_crypto
    r_utils
)
