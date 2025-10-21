import subprocess
import sys
import yaml
import argparse
from google.protobuf import descriptor_pb2


def run_protoc(proto_files, descriptor_out="desc.pb"):
    protoc_cmd = [
        "protoc",
        "--include_imports",
        f"--descriptor_set_out={descriptor_out}"
    ] + proto_files

    print("Running protoc:", " ".join(protoc_cmd))
    result = subprocess.run(protoc_cmd, capture_output=True, text=True)
    if result.returncode != 0:
        print("❌ protoc failed:")
        print(result.stderr)
        sys.exit(1)
    print("✅ protoc finished successfully.")


def get_call_type(method):
    if method.client_streaming and method.server_streaming:
        return "bidirectional-streaming-rpc"
    elif method.client_streaming:
        return "client-streaming-rpc"
    elif method.server_streaming:
        return "server-streaming-rpc"
    else:
        return "unary-rpc"


def parse_proto_descriptor(desc_file):
    with open(desc_file, 'rb') as f:
        desc_set = descriptor_pb2.FileDescriptorSet()
        desc_set.ParseFromString(f.read())

    all_services = []

    for file_desc in desc_set.file:
        package = file_desc.package or ""

        for service in file_desc.service:
            service_info = {
                "package": package,
                "service_name": service.name,
                "methods": []
            }

            for method in service.method:
                input_type = method.input_type.lstrip('.')
                output_type = method.output_type.lstrip('.')

                if package and input_type.startswith(package + "."):
                    input_type = input_type[len(package) + 1:]
                if package and output_type.startswith(package + "."):
                    output_type = output_type[len(package) + 1:]

                method_info = {
                    "name": method.name,
                    "input": input_type,
                    "output": output_type,
                    "type": get_call_type(method)
                }
                service_info["methods"].append(method_info)

            all_services.append(service_info)

    return all_services


def generate_yaml(services, extra_data):
    proto_files = extra_data.get("proto_files", [])
    proto_files.append("example/health.proto")
    yaml_dict = {
        "namespace": extra_data.get("namespace", ""),
        "server_class_name": extra_data.get("server_class_name", ""),
        "client_class_name": extra_data.get("client_class_name", ""),
        "include_grpc_files": extra_data.get("include_grpc_files", []),
        "package": services[0]["package"] if services else "",
        "proto_files": proto_files,
        "interface": {}
    }

    for service in services:
        yaml_dict["interface"][service["service_name"]] = service["methods"]

    return yaml.dump(yaml_dict, sort_keys=False, allow_unicode=True)


def main():
    parser = argparse.ArgumentParser(description="Parse proto files and generate YAML")

    parser.add_argument("proto_files", nargs="+", help="Proto files to process")
    parser.add_argument("--namespace", default="peak", help="Namespace for YAML")
    parser.add_argument("--server_class_name", default="GrpcServer", help="Server class name")
    parser.add_argument("--client_class_name", default="ClientServer", help="Client class name")
    parser.add_argument(
        "--include_grpc_files",
        nargs="*",
        default=["example.grpc.pb.h", "example.pb.h"],
        help="List of grpc include files"
    )
    parser.add_argument("--out", type=str, required=True, help="Path to output YAML file")

    args = parser.parse_args()

    run_protoc(args.proto_files, descriptor_out="desc.pb")
    services = parse_proto_descriptor("desc.pb")

    extra_data = {
        "namespace": args.namespace,
        "server_class_name": args.server_class_name,
        "client_class_name": args.client_class_name,
        "include_grpc_files": args.include_grpc_files,
        "proto_files": args.proto_files,
    }

    yaml_str = generate_yaml(services, extra_data)

    with open(args.out, "w", encoding="utf-8") as f:
        f.write(yaml_str)

    print(f"✅ YAML written to: {args.out}")


if __name__ == "__main__":
    main()
