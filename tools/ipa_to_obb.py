#!/usr/bin/env python3
"""Empacota a cópia local de The Sims 3 Ambitions em um OBB ZIP importável."""

import argparse
import hashlib
import os
from pathlib import Path, PurePosixPath
import struct
import sys
import tempfile
import zipfile


APP_PREFIX = "Payload/sims3dp_iphone.app/"
EXECUTABLE = APP_PREFIX + "sims3dp_iphone"
ARM64 = 0x0100000C
ARMV7 = 12
CHUNK = 1 << 20


def check_executable(data):
    """Validate every supported slice, including its FairPlay flag."""
    if len(data) < 28:
        raise ValueError("Executável Mach-O incompleto.")
    magic = struct.unpack_from(">I", data)[0]
    slices = []
    if magic == 0xCAFEBABE:
        count = struct.unpack_from(">I", data, 4)[0]
        if count > (len(data) - 8) // 20:
            raise ValueError("Tabela de arquiteturas Mach-O inválida.")
        for i in range(count):
            cpu, subtype, start, length, _ = struct.unpack_from(">5I", data, 8 + i * 20)
            if cpu == ARM64 or cpu == ARMV7 and subtype & 0xFFFFFF == 9:
                slices.append((cpu, start, length))
    elif magic in (0xCFFAEDFE, 0xCEFAEDFE):
        slices.append((struct.unpack_from("<I", data, 4)[0], 0, len(data)))
    if not slices:
        raise ValueError("O jogo não possui código armv7 ou arm64 compatível. Use a versão universal 1.1.84.")
    names = set()
    for cpu, offset, size in slices:
        check_slice(data, cpu, offset, size)
        names.add("arm64" if cpu == ARM64 else "armv7")
    return names


def check_slice(data, cpu, offset, size):
    is64 = cpu == ARM64
    header_size = 32 if is64 else 28
    if cpu not in (ARM64, ARMV7) or size < header_size or offset + size > len(data):
        raise ValueError("Fatia Mach-O incompleta.")
    header = struct.unpack_from("<8I" if is64 else "<7I", data, offset)
    if header[0] != (0xFEEDFACF if is64 else 0xFEEDFACE) or header[1] != cpu:
        raise ValueError("O jogo não possui um executável ARM válido.")
    if not is64 and header[2] & 0xFFFFFF != 9:
        raise ValueError("A fatia de 32 bits não é armv7.")
    commands, commands_size = header[4:6]
    end = offset + header_size + commands_size
    if end > offset + size or commands > commands_size // 8:
        raise ValueError("Comandos Mach-O inválidos.")
    at = offset + header_size
    for _ in range(commands):
        if at + 8 > end:
            raise ValueError("Comando Mach-O incompleto.")
        command, length = struct.unpack_from("<2I", data, at)
        if length < 8 or at + length > end:
            raise ValueError("Tamanho de comando Mach-O inválido.")
        if command == (0x2C if is64 else 0x21):
            if length < (24 if is64 else 20):
                raise ValueError("Comando de criptografia Mach-O incompleto.")
            if struct.unpack_from("<I", data, at + 16)[0] != 0:
                raise ValueError("O IPA ainda está criptografado (FairPlay). Use uma cópia descriptografada.")
        at += length
    if at != end:
        raise ValueError("Tamanho da tabela de comandos Mach-O inconsistente.")


def package(ipa, output):
    if ipa.resolve() == output.resolve():
        raise ValueError("O arquivo de saída deve ser diferente do IPA original.")
    if output.exists():
        raise ValueError(f"A saída já existe: {output}. Escolha outro nome para preservá-la.")
    if not output.parent.is_dir():
        raise ValueError(f"A pasta de saída não existe: {output.parent}")

    temporary = None
    try:
        with zipfile.ZipFile(ipa) as source:
            entries = []
            names = set()
            for entry in source.infolist():
                if not entry.filename.startswith(APP_PREFIX) or entry.is_dir():
                    continue
                relative = entry.filename[len(APP_PREFIX):]
                if not relative or relative.startswith("/") or ".." in PurePosixPath(relative).parts:
                    raise ValueError(f"Caminho inválido no IPA: {entry.filename}")
                if entry.filename in names:
                    raise ValueError(f"Entrada duplicada no IPA: {entry.filename}")
                if (entry.external_attr >> 16) & 0o170000 == 0o120000:
                    raise ValueError(f"Link simbólico não suportado: {entry.filename}")
                names.add(entry.filename)
                entries.append(entry)
            if EXECUTABLE not in names:
                raise ValueError("Este IPA não contém Payload/sims3dp_iphone.app/sims3dp_iphone.")
            architectures = check_executable(source.read(EXECUTABLE))
            print("Arquiteturas verificadas: " + ", ".join(sorted(architectures)), flush=True)

            total = sum(entry.file_size for entry in entries)
            print(f"Empacotando {len(entries)} arquivos ({total / (1 << 20):.1f} MiB extraídos)…", flush=True)
            with tempfile.NamedTemporaryFile(prefix=output.name + ".", suffix=".partial",
                                             dir=output.parent, delete=False) as tmp:
                temporary = Path(tmp.name)
            digests = {}
            done, last_progress = 0, -1
            with zipfile.ZipFile(temporary, "w", compression=zipfile.ZIP_DEFLATED,
                                 compresslevel=6) as dest:
                for entry in sorted(entries, key=lambda item: item.filename):
                    info = zipfile.ZipInfo(entry.filename, entry.date_time)
                    info.compress_type = zipfile.ZIP_DEFLATED
                    digest = hashlib.sha256()
                    with source.open(entry) as src, dest.open(info, "w") as dst:
                        while chunk := src.read(CHUNK):
                            dst.write(chunk)
                            digest.update(chunk)
                            done += len(chunk)
                    digests[entry.filename] = digest.digest()
                    progress = int(done * 100 / total) if total else 100
                    if progress // 10 > last_progress // 10:
                        print(f"  {progress}%", flush=True)
                        last_progress = progress

            # Read every output file: verify CRC and byte-for-byte content via SHA-256.
            print("Verificando o OBB…", flush=True)
            with zipfile.ZipFile(temporary) as dest:
                if set(dest.namelist()) != names:
                    raise ValueError("A lista de arquivos do OBB não corresponde ao IPA.")
                for name, expected in digests.items():
                    digest = hashlib.sha256()
                    with dest.open(name) as src:
                        while chunk := src.read(CHUNK):
                            digest.update(chunk)
                    if digest.digest() != expected:
                        raise ValueError(f"Conteúdo divergente no OBB: {name}")
            # Link atomically without overwriting an output created in the meantime.
            os.link(temporary, output)
            print(f"OBB criado: {output} ({output.stat().st_size / (1 << 20):.1f} MiB)")
            print("No Android, selecione este arquivo no importador do app; não precisa copiar para Android/obb.")
    finally:
        if temporary is not None:
            temporary.unlink(missing_ok=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("ipa", type=Path, help="IPA local, versão 1.1.84 descriptografada")
    parser.add_argument("obb", type=Path, help="OBB de saída (ZIP com os arquivos do jogo)")
    args = parser.parse_args()
    try:
        package(args.ipa, args.obb)
    except (OSError, ValueError, RuntimeError, zipfile.BadZipFile) as error:
        print(f"Erro: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
