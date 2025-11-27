# C++ gRPC Code Auto-Generator

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)

This project provides a Python script to automatically generate a C++ gRPC project skeleton from protobuf files. It uses the Jinja2 templating engine, making it easy to customize the output code.

## Features

- **Template-driven**: Uses Jinja2 templates to generate code, allowing for easy customization of the output.
- **Generates a Complete Project**: Creates a full project structure with:
  - A generic gRPC server wrapper.
  - A main server implementation (`server.cpp`).
  - A client implementation for each defined service.
  - `xmake.lua` for easy building and dependency management.
- **Automatic Proto File Handling**: Copies the specified `.proto` files into the generated project.
- **Code Formatting**: Automatically formats the generated C++ code using `clang-format` if available.

## Prerequisites

- Python 3.x
- The Python packages listed in `requirements.txt`.
- A C++ compiler(Need to support C++20) and `xmake` installed to build the generated project.

## Installation

1.  Clone this repository.
2.  Install the required Python packages:
    ```bash
    python3 -m venv env
    source env/bin/activate
    pip install -r requirements.txt
    ```

## Quick Start

This example shows how to generate a complete gRPC project from the `example.proto` file.

1.  **Generate the project:**

    This script performs two main steps:

    a.  **`proto2yaml.py`**: Parses the `.proto` file to create an intermediate `proto.yaml` file that describes the gRPC services and messages.

        python3 proto2yaml.py example/example.proto \
            --namespace peak \
            --server_class_name GrpcServer \
            --client_class_name GrpcClient \
            --include_grpc_files example.grpc.pb.h example.pb.h \
            --out ./proto.yaml

    b.  **`cpp-grpc-auto-gen.py`**: Uses the `proto.yaml` file and Jinja2 templates to generate the complete C++ project source code, including a server, client, and `xmake.lua` build file.

        python3 cpp-grpc-auto-gen.py \
            --proto proto.yaml \
            --template ./template \
            --out_server_file example/include/grpc_server.hpp \
            --out_client_file example/include/grpc_client.hpp \
            --format=clang-format

2.  **Build and run the generated project:**

    ```bash
    cd example_project
    xmake build
    xmake install -o .
    ./bin/server
    ```

    You can run the client in another terminal:

    ```bash
    ./bin/example_client
    ```

## Generated Project Structure

The generated project in the `example_project/` directory will have the following structure:

```
example_project/
├── include/
│   ├── grpc_client.hpp
│   └── grpc_server.hpp
├── proto/
│   ├── example.proto
│   └── health.proto
├── src/
│   ├── example_client.cpp
│   └── server.cpp
└── xmake.lua
```

-   `include/`: Contains the generated gRPC server and client header files.
-   `proto/`: Contains the `.proto` files used to generate the code.
-   `src/`: Contains the main server and client implementation files.
-   `xmake.lua`: The build file for the project.

## License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.