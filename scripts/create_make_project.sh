# Run from esmini root folder: ./scripts/create_make_project.sh

mkdir -p build
cd build
cmake -G "Unix Makefiles" -D USE_OSG=True ..
