# CDN Test App

A minimal app to check the CDN/Network performances.

Version : 0.1

### Notes:
Version 0.1 supports only few formats of HLS. Needs improvements to support other formats
Future plan to support MPEG-DASH.

### Build on Ubuntu
1. sudo apt-get install g++
2. sudo apt install libcurl4-openssl-dev
3. g++ -std=c++17 cdn_check_app.cpp -o cdn_check_app -lcurl

#### Run Sample
./cdn_check_app hls_test_vectors.json stats 3

#### Output interpretation
Given in the sample.xlsx