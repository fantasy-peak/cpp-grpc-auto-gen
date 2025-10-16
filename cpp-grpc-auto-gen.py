import os
import yaml
from jinja2 import Environment, FileSystemLoader
import argparse
import subprocess
import stringcase
import shutil

def get_filename(path):
    """Extracts the filename from a path."""
    return os.path.basename(path)

def load_yaml(yaml_path):
    """Loads a YAML file."""
    with open(yaml_path, encoding="utf-8") as f:
        return yaml.load(f, Loader=yaml.FullLoader)

def render_write_format(template_name, output_path, render_config, jenv, formatter):
    """Renders a template, writes it to a file, and optionally formats it."""
    template = jenv.get_template(template_name)
    content = template.render(grpc=render_config)
    with open(output_path, 'w') as f:
        f.write(content)
    
    if formatter and formatter.lower() != 'none':
        try:
            subprocess.run([formatter, "-i", output_path], check=True)
            print(f"Generated and formatted {output_path}")
        except FileNotFoundError:
            print(f"Warning: Formatter '{formatter}' not found. Skipping formatting for {output_path}")
    else:
        print(f"Generated {output_path}")

def copy_proto_files(proto_files, dest_dir):
    """Copies proto files to the destination directory."""
    if not proto_files:
        print("No proto files specified in the config (expected 'proto_files' key). Skipping copy.")
        return
    for proto_file in proto_files:
        if os.path.exists(proto_file):
            shutil.copy(proto_file, dest_dir)
            print(f"Copied {proto_file} to {dest_dir}")
        else:
            print(f"Warning: Proto file not found: {proto_file}")

if __name__ == "__main__":
    parser = argparse.ArgumentParser(
        description="Generate C++ gRPC server and client code from a YAML configuration.",
        formatter_class=argparse.RawTextHelpFormatter
    )
    parser.add_argument('--proto', type=str, default='proto.yaml', help='Path to the proto config file (e.g., proto.yaml)')
    parser.add_argument('--template', type=str, default='template/grpc_server.j2', help='Path to the main Jinja2 template file')
    parser.add_argument('--format', type=str, default='clang-format', help='Code formatter command (e.g., clang-format). Use "none" to disable.')
    parser.add_argument('--out_file', type=str, required=True, help='Output path for the main generated header file')
    args = parser.parse_args()

    # --- Setup ---
    config = load_yaml(args.proto)
    template_dir = os.path.dirname(args.template)
    loader = FileSystemLoader(searchpath=template_dir if template_dir else "./")
    jenv = Environment(loader=loader, extensions=['jinja2_strcase.StrcaseExtension'])
    jenv.filters['get_filename'] = get_filename

    # --- Main File Generation ---
    config['out_file'] = args.out_file
    config['package'] = config.get('package', '').replace('.', '::')
    
    main_template_name = os.path.basename(args.template)
    render_write_format(main_template_name, args.out_file, config, jenv, args.format)
    print(f"\nMain header file generated at: {args.out_file}")

    # --- Example Project Generation ---
    print("\n--- Generating Project ---")
    example_dir = "example_project"
    src_dir = os.path.join(example_dir, "src")
    include_dir = os.path.join(example_dir, "include")
    proto_dir = os.path.join(example_dir, "proto")

    for d in [src_dir, include_dir, proto_dir]:
        os.makedirs(d, exist_ok=True)

    shutil.copy(args.out_file, include_dir)
    print(f"Copied generated header to {include_dir}")

    # Assumes 'proto_files' key in YAML, e.g.:
    # proto_files:
    #   - path/to/your.proto
    copy_proto_files(config.get('proto_files', []), proto_dir)

    # Generate xmake.lua, server, and client files
    render_write_format("xmake.j2", os.path.join(example_dir, "xmake.lua"), config, jenv, None)
    render_write_format("server.cpp.j2", os.path.join(src_dir, "server.cpp"), config, jenv, args.format)

    for service_name, method_list in config.get('interface', {}).items():
        tmp_config = config.copy()
        tmp_config['interface'] = {service_name: method_list}
        client_out_path = os.path.join(src_dir, f"{stringcase.snakecase(service_name)}_client.cpp")
        render_write_format("client.cpp.j2", client_out_path, tmp_config, jenv, args.format)
    
    print("\nproject generation complete.")