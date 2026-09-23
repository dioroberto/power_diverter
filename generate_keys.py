#!/usr/bin/env python3

from pathlib import Path
import re

from cryptography.hazmat.primitives.asymmetric.x25519 import X25519PrivateKey
from cryptography.hazmat.primitives import serialization


# ============================================================
# Configuration
# ============================================================

ROOT = Path(__file__).resolve().parent

CONFIG_HEADER = ROOT / "config" / "udp_config.h"

KEYS_DIR = ROOT / "keys"

ESP32_KEY_DIR = KEYS_DIR / "esp32"
SERVER_KEY_DIR = KEYS_DIR / "server"

P1_INCLUDE_DIR = ROOT / "p1-device" / "include"
SERVER_INCLUDE_DIR = ROOT / "udp-server" / "include"

SECRETS_UDP_HEADER = P1_INCLUDE_DIR / "secrets_udp.h"


# ============================================================
# Read device name from secrets_udp.h
# ============================================================

def get_udp_config() -> tuple[str, str, int]:
    if not CONFIG_HEADER.exists():
        raise FileNotFoundError(
            f"UDP configuration not found: {CONFIG_HEADER}"
        )

    content = CONFIG_HEADER.read_text()

    device_match = re.search(
        r'constexpr\s+const\s+char\s*\*\s*DEVICE_NAME\s*='
        r'\s*"([^"]+)"',
        content,
    )

    server_match = re.search(
        r'constexpr\s+const\s+char\s*\*\s*SERVER_NAME\s*='
        r'\s*"([^"]+)"',
        content,
    )

    port_match = re.search(
        r'constexpr\s+uint16_t\s+SERVER_PORT\s*='
        r'\s*(\d+)',
        content,
    )

    if not device_match:
        raise RuntimeError(
            "DEVICE_NAME not found in config/udp_config.h"
        )

    if not server_match:
        raise RuntimeError(
            "SERVER_NAME not found in config/udp_config.h"
        )

    if not port_match:
        raise RuntimeError(
            "SERVER_PORT not found in config/udp_config.h"
        )

    device_name = device_match.group(1)
    server_name = server_match.group(1)
    server_port = int(port_match.group(1))

    if len(device_name) > 16:
        raise ValueError(
            f"DEVICE_NAME '{device_name}' exceeds "
            "the UDP protocol limit of 16 characters"
        )

    return device_name, server_name, server_port


# ============================================================
# Helpers
# ============================================================

def raw_private_key(key: X25519PrivateKey) -> bytes:
    return key.private_bytes(
        encoding=serialization.Encoding.Raw,
        format=serialization.PrivateFormat.Raw,
        encryption_algorithm=serialization.NoEncryption(),
    )


def raw_public_key(key: X25519PrivateKey) -> bytes:
    return key.public_key().public_bytes(
        encoding=serialization.Encoding.Raw,
        format=serialization.PublicFormat.Raw,
    )


def cpp_array(data: bytes, indent: str = "        ") -> str:
    lines = []

    for i in range(0, len(data), 8):
        chunk = data[i:i + 8]

        lines.append(
            indent
            + ", ".join(f"0x{byte:02X}" for byte in chunk)
            + ","
        )

    return "\n".join(lines)


def write_binary(path: Path, data: bytes) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data)


# ============================================================
# Generate P1 device keys
# ============================================================

def generate_device_keys():
    private_key = X25519PrivateKey.generate()

    private_bytes = raw_private_key(private_key)
    public_bytes = raw_public_key(private_key)

    write_binary(
        ESP32_KEY_DIR / "device_private.key",
        private_bytes,
    )

    write_binary(
        ESP32_KEY_DIR / "device_public.key",
        public_bytes,
    )

    return private_bytes, public_bytes


# ============================================================
# Generate UDP server keys
# ============================================================

def generate_server_keys():
    private_key = X25519PrivateKey.generate()

    private_bytes = raw_private_key(private_key)
    public_bytes = raw_public_key(private_key)

    write_binary(
        SERVER_KEY_DIR / "server_private.key",
        private_bytes,
    )

    write_binary(
        SERVER_KEY_DIR / "server_public.key",
        public_bytes,
    )

    return private_bytes, public_bytes


# ============================================================
# Generate P1 secrets_udp.h
# ============================================================

def generate_p1_secrets(
    device_name: str,
    device_private_key: bytes,
    server_public_key: bytes,
):
    P1_INCLUDE_DIR.mkdir(parents=True, exist_ok=True)

    content = f"""#pragma once

#include <stdint.h>

namespace SecretsUDP
{{
    namespace Device
    {{
        constexpr const char* NAME =
            "{device_name}";
    }}

    namespace UDP
    {{
        /*
         * X25519 private key belonging to this P1 device.
         *
         * KEEP SECRET.
         */
        static constexpr uint8_t DEVICE_PRIVATE_KEY[32] = {{
{cpp_array(device_private_key)}
        }};

        /*
         * X25519 public key belonging to the UDP server.
         *
         * This is NOT secret.
         */
        static constexpr uint8_t SERVER_PUBLIC_KEY[32] = {{
{cpp_array(server_public_key)}
        }};
    }}
}}
"""

    path = P1_INCLUDE_DIR / "secrets_udp.h"
    path.write_text(content)

    return path


# ============================================================
# Generate UDP server server_keys.h
# ============================================================

def generate_server_keys_header(
    device_name: str,
    server_private_key: bytes,
    device_public_key: bytes,
):
    SERVER_INCLUDE_DIR.mkdir(parents=True, exist_ok=True)

    content = f"""#pragma once

#include <stdint.h>

namespace ServerSecrets
{{
    /*
     * X25519 private key belonging to this UDP server.
     *
     * KEEP SECRET.
     */
    static constexpr uint8_t SERVER_PRIVATE_KEY[32] = {{
{cpp_array(server_private_key)}
    }};

    /*
     * X25519 public key belonging to the ESP32-P1 device.
     *
     * This is NOT secret.
     */
    static constexpr uint8_t ESP32_P1_PUBLIC_KEY[32] = {{
{cpp_array(device_public_key)}
    }};

    constexpr const char* DEVICE_NAME =
        "{device_name}";
}}
"""

    path = SERVER_INCLUDE_DIR / "server_keys.h"
    path.write_text(content)

    return path


# ============================================================
# Main
# ============================================================

def main():
    device_name, server_name, server_port = get_udp_config()

    print("Generating X25519 key pairs...")
    print(f"Device name: {device_name}")

    device_private_key, device_public_key = generate_device_keys()
    server_private_key, server_public_key = generate_server_keys()

    p1_secrets = generate_p1_secrets(
        device_name,
        device_private_key,
        server_public_key,
    )

    server_secrets = generate_server_keys_header(
        device_name,
        server_private_key,
        device_public_key,
    )

    # Store the device public key on the server side as a
    # separate binary file as well.
    write_binary(
        SERVER_KEY_DIR / f"{device_name}.public.key",
        device_public_key,
    )

    print()
    print("Keys generated successfully.")
    print()
    print("P1 device:")
    print(f"  private: {ESP32_KEY_DIR / 'device_private.key'}")
    print(f"  public:  {ESP32_KEY_DIR / 'device_public.key'}")
    print(f"  header:  {p1_secrets}")
    print()
    print("UDP server:")
    print(f"  private: {SERVER_KEY_DIR / 'server_private.key'}")
    print(f"  public:  {SERVER_KEY_DIR / 'server_public.key'}")
    print(
        f"  device:  "
        f"{SERVER_KEY_DIR / f'{device_name}.public.key'}"
    )
    print(f"  header:  {server_secrets}")
    print()
    print(f"Device name: {device_name}")


if __name__ == "__main__":
    main()