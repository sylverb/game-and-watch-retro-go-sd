#!/bin/bash

# Default to 1000 if variables are not set
USER_ID=${HOST_UID:-1000}
GROUP_ID=${HOST_GID:-1000}
USER_NAME=builder

# Change UID_MIN so Mac UIDs don't through an error
sed -i 's/^UID_MIN.*/UID_MIN           500/' /etc/login.defs

# Create group if it doesn't exist
if ! getent group "$GROUP_ID" >/dev/null; then
    groupadd -g "$GROUP_ID" "$USER_NAME"
fi

# Create user if it doesn't exist
if ! getent passwd "$USER_ID" >/dev/null; then
    useradd -u "$USER_ID" -g "$GROUP_ID" \
        -d /opt/workdir \
        -G plugdev \
        "$USER_NAME"
    # Allow the user to use sudo without a password
    echo "$USER_NAME ALL=(ALL) NOPASSWD:ALL" >> /etc/sudoers
fi

# Use gosu to drop privileges and run the CMD
exec /usr/sbin/gosu "$USER_NAME" "$@"
