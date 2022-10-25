#!/bin/sh

# Install Ceres
sudo apt-get -y install cmake
sudo apt-get -y install libgoogle-glog-dev
sudo apt-get -y install libatlas-base-dev
sudo apt-get -y install libsuitesparse-dev
mkdir ceres
cd ceres
wget https://ceres-solver.googlesource.com/ceres-solver/+archive/1.14.0.tar.gz
tar zxf 1.14.0.tar.gz
rm 1.14.0.tar.gz
mkdir ceres-bin
cd ceres-bin
cmake ../
sudo make
sudo make install
cd ../../
rm -rf ceres

# Install Eigen
git clone https://gitlab.com/libeigen/eigen.git
cd eigen
git checkout 3.3.7
mkdir build_dir
cd build_dir
cmake ../
sudo make install
cd ../../
rm -rf eigen/

# Install CppAD
git clone https://github.com/coin-or/CppAD.git
mkdir CppAD/build_dir
cd CppAD/build_dir
cmake ../
sudo make install
cd ../../
rm -rf CppAD/
