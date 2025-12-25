# Run from esmini root folder: ./scripts/create_xcode_project.sh

mkdir -p build
cd build
cmake -G Xcode -D USE_OSG=True ..
