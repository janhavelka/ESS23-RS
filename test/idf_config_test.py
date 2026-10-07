"""Compile the actual USB adapter with valid and console-corrupting SDK settings."""
import subprocess
import sys
import tempfile
from pathlib import Path


def main():
    compiler, compiler_id = sys.argv[1:]
    root = Path(__file__).resolve().parents[1]
    source = root / 'examples/probe_idf/main/IdfPlatform.cpp'
    includes = [root / 'test/fakes/esp32_uart', root / 'examples/probe_cli']
    with tempfile.TemporaryDirectory() as temporary:
        if compiler_id == 'MSVC':
            command = [compiler, '/nologo', '/c', '/std:c++14',
                       '/DCONFIG_IDF_TARGET_ESP32S3=1',
                       *['/I' + str(path) for path in includes],
                       '/Fo' + str(Path(temporary) / 'platform.obj'), str(source)]
            debug_option = '/DCONFIG_GPTIMER_ENABLE_DEBUG_LOG=1'
        else:
            command = [compiler, '-std=c++11', '-fsyntax-only',
                       '-DCONFIG_IDF_TARGET_ESP32S3=1',
                       *['-I' + str(path) for path in includes], str(source)]
            debug_option = '-DCONFIG_GPTIMER_ENABLE_DEBUG_LOG=1'
        normal = subprocess.run(command, capture_output=True, text=True)
        assert normal.returncode == 0, normal.stdout + normal.stderr
        debug = subprocess.run(command + [debug_option], capture_output=True, text=True)
        assert debug.returncode != 0, 'Forced GPTimer logging must prevent firmware compilation'
        assert 'SDK logging must remain disabled' in debug.stdout + debug.stderr
        arduino = command.copy()
        arduino[arduino.index(str(source))] = str(root / 'examples/probe_cli/ArduinoPlatform.cpp')
        prefix = '/D' if compiler_id == 'MSVC' else '-D'
        normal = subprocess.run(arduino + [prefix + 'ARDUINO_USB_CDC_ON_BOOT=0'], capture_output=True, text=True)
        assert normal.returncode == 0, normal.stdout + normal.stderr
        competing = subprocess.run(arduino + [prefix + 'ARDUINO_USB_CDC_ON_BOOT=1'], capture_output=True, text=True)
        assert competing.returncode != 0, 'Arduino HWCDC must not compete with the shared IDF USB driver'
        assert 'Arduino CDC auto-start must be disabled' in competing.stdout + competing.stderr
    print('Native SDK configuration and exclusive Arduino USB-driver ownership verified')


if __name__ == '__main__':
    main()
