idf_component_register(SRCS "main.cpp"
                    INCLUDE_DIRS "include"
                    EMBED_TXTFILES "include/root_certs.pem")

target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)