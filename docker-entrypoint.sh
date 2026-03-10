#!/bin/sh
# Entrypoint script — dispatches to the correct binary
case "$1" in
    wallet_cli|genesis_tool)
        exec "$@"
        ;;
    *)
        exec chronos_node "$@"
        ;;
esac
