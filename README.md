# Yakasaur-ESPIDF

## Overview

This is a demo version of the embedded software running on a Yakasaur Rover.
This project is a work in progress and might be a bit messy in its current
state. The primary purpose of this software is to learn enough ESP-IDF as a
proof-of-concept for building a Yakasaur Rover. The project is currently
configured to work with a specific board, but it can be easily modified to work
with other boards.

## Highlights

1. **Work in Progress**: This repository is currently a demo and might be a bit unorganized. It's actively being developed, so expect frequent changes.
2. **ESP-IDF**: The project is built using esp-idf and includes a devcontainer for a build environment.
3. **Configuration**: All secrets and configurations are sourced from `local.h`. This file is mandatory for the project to function correctly. A template named `local.h.sample` is provided to help you set up your own `local.h` file. Ensure you never commit the `local.h` file to the repository.
4. **Yak GDS Integration**: This demo assumes that Yakasaur will connect to an API server named Yak GDS. You can find the repository for Yak GDS [here](https://github.com/rhettg/yakgds).
5. **Related Repositories and Resources**:
   - [Yakasaur Main Repository](https://github.com/The-Yak-Collective/yakasaur)
   - [PlatformIO](https://platformio.org)
   - [ESP-IDF Documentation](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/index.html)
   - [ESP32 Camera Repository](https://github.com/espressif/esp32-camera)
   - [Information on the Freenove ESP32S3 WROOM Board](https://github.com/Freenove/Freenove_ESP32_S3_WROOM_Board)

## How to Use

1. **Clone the Repository**:
   ```bash
   git clone --recursive https://github.com/rhettg/Yakasaur-espidf.git
   ```

This should likely go into your platform.io folder, which for me is `~/Documents/PlatformIO/Projects/`
Use `--recursive` to ensure the submodules (`esp32-camera`) are cloned as well.

2. Build and Open in Devcontainer

3. **Setup Configuration**:
   - Copy the `local.h.sample` file to `local.h`.
   - Fill in the required details such as wifi and api endpoints.

4. **Integration with Yak GDS**:
   - Ensure the Yak GDS server is running and accessible.
   - The Yakasaur will attempt to connect to the Yak GDS server using the URL provided in the `local.h` file.

5. **Build and Upload Image**
   - Use `idf.py build` to create firmware.
   - Flash your device.
   - Use the Serial Monitor to view the output of the program.

6. **Control Yakasaur Remotely**:
   - Using Yak GDS, send commands to your "Rover".


## Feedback and Contributions

Feedback, issues, and pull requests are always welcome! If you encounter any problems or have suggestions, please open an issue on the GitHub repository.

Remember, this is a work in progress, and your contributions can help improve it!
