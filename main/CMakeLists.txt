idf_component_register(SRCS "main.cpp"
                    INCLUDE_DIRS "include"
                    EMBED_TXTFILES "include/firebaseio_root_ca.pem")

target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)