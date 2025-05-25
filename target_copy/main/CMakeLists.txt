# Collect all .c files in the specified directories
file(GLOB SRC_FILES 
     "*.c" 
     "blufi/*.c"
     "logs/*.c"
     "websocket/*.c"
     "time/*.c"
     "lvgl/*.c"
     "peripheral/*.c"
)

# Define include directories
set(INCLUDE_DIRS 
    "." 
    "blufi" 
    "logs" 
    "websocket"
    "time"
    "lvgl"
    "peripheral"
)

# Initialize an empty list for files to embed
set(EMBED_FILES "")

# Conditionally append files to the list based on configuration
#if(CONFIG_WS_OVER_TLS_MUTAL_AUTH)
list(APPEND EMBED_FILES
        "certs/client_cert.pem"
        "certs/ca_cert.pem"
        "certs/client_key.pem"
        "certs/thinkfuture.pem")
#endif()
# list(APPEND EMBED_FILES "certs/isrg_root_x1.pem")
# Register the component with source files, include dirs, and any conditionally added embedded files
idf_component_register(
    SRCS ${SRC_FILES}  # Use the list of collected source files
    INCLUDE_DIRS ${INCLUDE_DIRS}
    EMBED_TXTFILES ${EMBED_FILES} 
)
# 在你的 CMakeLists.txt 添加这个，取消这个警告视为错误
add_compile_options(-Wno-error=switch)
