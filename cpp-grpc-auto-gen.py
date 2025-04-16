import os
import yaml
import json
from jinja2 import *
import argparse
import subprocess

def get_filename(path):
    return path.split('/')[-1]

def load_yaml(yaml_path):
    with open(yaml_path, encoding="utf-8") as f:
        datas = yaml.load(f,Loader=yaml.FullLoader)
    json_datas = json.dumps(datas, indent=5)
    return json.loads(json_datas)

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="cpp-grpc-auto-gen")
    parser.add_argument('--proto', type=str, default='./proto.yaml', help='proto config file')
    parser.add_argument('--template', type=str, default='./template/grpc_server.j2', help='template file')
    parser.add_argument('--format', type=str, default='clang-format', help='clang-format')
    parser.add_argument('--out_file', type=str, required=True, help='output file')
    args = parser.parse_args()

    yaml_path = args.proto
    loader = FileSystemLoader(searchpath="./")
    jenv = Environment(loader=loader, extensions=['jinja2_strcase.StrcaseExtension'])
    jenv.filters['get_filename'] = get_filename
    template = jenv.get_template(args.template)
    config = load_yaml(yaml_path)
    config['out_file'] = args.out_file
    config['package'] = config['package'].replace('.', '::')
    htmlout = template.render(grpc=config)
    print(htmlout)
    with open(config['out_file'], 'w') as file:
        file.write(htmlout)
    os.system('{} -i {}'.format(args.format, config['out_file']))

    # create example
    dir_path = "example_project"
    os.makedirs(dir_path, exist_ok=True)

    dir_path = "example_project/proto"
    os.makedirs(dir_path, exist_ok=True)

    dir_path = "example_project/src"
    os.makedirs(dir_path, exist_ok=True)

    dir_path = "example_project/include"
    os.makedirs(dir_path, exist_ok=True)
    subprocess.run(["cp", config['out_file'], dir_path], check=True)

    dir_path = "example_project"

    template = jenv.get_template("./template/xmake.j2")
    htmlout = template.render(grpc=config)

    with open(dir_path + '/' + 'xmake.lua', 'w') as file:
        file.write(htmlout)

    template = jenv.get_template("./template/main.cpp.j2")
    htmlout = template.render(grpc=config)

    with open(dir_path + '/src/' + 'main.cpp', 'w') as file:
        file.write(htmlout)
    