#!/bin/sh
set -ex

g++ main.cpp -o main -g
sudo ./main
