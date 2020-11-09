idf_component_register(
    INCLUDE_DIRS "."
    REQUIRES "arduino"
)

target_compile_features(${COMPONENT_LIB} INTERFACE cxx_std_17)