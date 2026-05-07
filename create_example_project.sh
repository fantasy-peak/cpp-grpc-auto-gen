export PATH=/root/.xmake/packages/p/protobuf-cpp/33.2/b3738b6a93ec47949689248d7fcccfaa/bin:$PATH
python3 proto2yaml.py proto/example.proto \
    --namespace peak \
    --server_class_name GrpcServer \
    --client_class_name GrpcClient \
    --include_grpc_files example.grpc.pb.h example.pb.h \
    --out ./proto.yaml

python3 cpp-grpc-auto-gen.py \
    --proto proto.yaml \
    --template ./template \
    --out_server_file example/include/grpc_server.hpp \
    --out_client_file example/include/grpc_client.hpp \
    --example ./example_project \
    --format=clang-format

rm desc.pb

cd example_project
pwd
xmake build -j 16
xmake install -o . -v
