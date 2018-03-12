#! /bin/bash

export RESULT_BINARY="./bin"
export RESULT_OTHER_BINARY="./bin/other"

export BINARY_NAME="proxy"
export BINARY_WITH_ALL_SYMBOLS_NAME="proxy_with_all_symbols"
export BINARY_WITHOUT_DEBUG_SYMBOLS_NAME="proxy_without_debug_symbols"

export PATH_ALL_SYMBOLS="./"
export PATH_WITHOUT_DEBUG=""

if [ -f "${BINARY_NAME}" ]; then
    rm -f "${BINARY_NAME}"
    rm -fr "${RESULT_BINARY}"
fi

echo "Compiling..."

# -DUSE_FULL_DEBUG
# -DUSE_FULL_DEBUG_POLL_INTERVAL
# -DPOLLING_REQUESTS_SIZE
# -DDATA_BUFFER_SIZE
# -D__USER_DEFAULT_PROXY_PORT
# -D__USER_DEFAULT_SERVER_PORT
# -D__USER_DEFAULT_SERVER_IP
# -D__USER_DEFAULT_CLIENT_POLL_TIMEOUT
# -D__USER_DEFAULT_SERVER_POLL_TIMEOUT
# -D__USER_DEFAULT_WORKER_POLL_TIMEOUT
# -D__USER_DEFAULT_CLIENT_KEEP_ALIVE
# -D__USER_DEFAULT_SERVER_KEEP_ALIVE
# -D__USER_DEFAULT_CLIENT_TCP_NO_DELAY
# -D__USER_DEFAULT_SERVER_TCP_NO_DELAY

g++ -Wall \
    -Wextra \
    -gdwarf-4 \
    -std=gnu++17 \
    -fvar-tracking-assignments \
    -O0 \
    -m64 \
    -mtune=native \
    -march=native \
    -mfpmath=sse \
    -lpthread \
    -lm \
    -lc \
    main.cpp \
    log.cpp \
    daemon.cpp \
    proxy_impl.cpp \
    server_worker.cpp \
    client_worker.cpp \
    worker_worker.cpp \
    client_logic.cpp \
    server_logic.cpp \
    worker_logic.cpp \
    -o "${BINARY_NAME}"

if [ -f "${BINARY_NAME}" ]; then
    echo "OK!"

    echo "Prepare for strip, etc..."

    mkdir -p ${RESULT_BINARY}
    mkdir -p ${RESULT_OTHER_BINARY}

    cp ${BINARY_NAME} \
    ${RESULT_BINARY}/${BINARY_NAME}

    cp ${BINARY_NAME} \
    ${RESULT_OTHER_BINARY}/${BINARY_WITH_ALL_SYMBOLS_NAME}

    cp ${BINARY_NAME} \
    ${RESULT_OTHER_BINARY}/${BINARY_WITHOUT_DEBUG_SYMBOLS_NAME}

    echo "Ok!"

    echo "Stripping..."

    strip --strip-all \
    ${RESULT_BINARY}/${BINARY_NAME}

    strip --strip-debug \
    ${RESULT_OTHER_BINARY}/${BINARY_WITHOUT_DEBUG_SYMBOLS_NAME}

    echo "OK!"
else
    echo "ERROR!"
fi

# ##############################################################################
# End of file
# ##############################################################################
