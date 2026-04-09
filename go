rm build-release/cvt
inpfile=${1:-"TestInputFiles/polydisperse.osph"}
cmake --build --preset release
./build-release/cvt $inpfile
