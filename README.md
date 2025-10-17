# C++ gRPC Code Auto-Generator

This project provides a Python script to automatically generate a C++ gRPC project skeleton from a YAML configuration file. It uses the Jinja2 templating engine, making it easy to customize the output code.

## Features

- **YAML-based Configuration**: Define your services, RPC methods, and proto files in a simple YAML file.
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

## Installation

1.  Clone this repository.
2.  Install the required Python packages:
    ```bash
    python3 -m venv env
    source env/bin/activate
    pip install -r requirements.txt
    ```
3.  Ensure you have a C++ compiler and `xmake` installed to build the generated project.

## Usage

The main script `cpp-grpc-auto-gen.py` is used to generate the code. The script has been updated to simplify its execution by generating a complete, runnable project.

1.  **Define your service in a YAML file.** Create a file (e.g., `proto.yaml`) to define your services, methods, and related files. See the example below.

2.  **Run the generation script.** Execute the script, pointing it to your YAML config, the main template, and the desired output header file. The script will then generate a complete project in the `example_project/` directory.

    ```bash
    python3 cpp-grpc-auto-gen.py \
      --proto proto.yaml \
      --template template/grpc_server.j2 \
      --out_server_file example/include/grpc_server.hpp \
      --out_client_file example/include/grpc_client.hpp \
      --format=clang-format
    ```

3.  **Build and run the generated project:**
    ```bash
    cd example_project
    xmake build
    xmake install -o .
    ./bin/server
    ```

## YAML Configuration Example

Here is an example of a `proto.yaml` file:

```yaml
---
namespace: peak
server_class_name: GrpcServer
client_class_name: ClientServer
include_grpc_files: [example.grpc.pb.h, example.pb.h]
package: fantasy.v1
proto_files: [example/example.proto, example/health.proto]

interface:
  Example:
    - name: Notice
      input: NoticeRequest
      output: NoticeResponse
      type: bidirectional-streaming-rpc

    - name: Order
      input: OrderRequest
      output: OrderResponse
      type: unary-rpc

    - name: GetOrderSeqNo
      input: GetOrderSeqNoRequest
      output: GetOrderSeqNoResponse
      type: unary-rpc

    - name: ServerStreaming
      input: OrderRequest
      output: OrderResponse
      type: server-streaming-rpc

    - name: ClientStreaming
      input: OrderRequest
      output: OrderResponse
      type: client-streaming-rpc

```

- `namespace`: The C++ namespace for the generated server class.
- `server_class_name`: The name of the main generated server class.
- `client_class_name`: The name of the main generated client class.
- `include_grpc_files`: A list of header files to include in the main generated header.
- `package`: The gRPC package name from your proto definition.
- `proto_files`: A list of `.proto` files to be copied into the generated project.
- `interface`: A dictionary defining the services and their methods.
  - `name`: The RPC method name.
  - `input`: The request message type.
  - `output`: The response message type.
  - `type`: The gRPC method type (`unary-rpc`, `server-streaming-rpc`, `client-streaming-rpc`, `bidirectional-streaming-rpc`).

## Customization

You can customize the generated code by modifying the Jinja2 templates located in the `template/` directory:
- `grpc_server.hpp.j2`: Template for the main server header.
- `grpc_client.hpp.j2`: Template for the client header.
- `server.cpp.j2`: Template for the server's main implementation.
- `client.cpp.j2`: Template for the auto-generated clients.
- `xmake.j2`: Template for the `xmake.lua` build file.
