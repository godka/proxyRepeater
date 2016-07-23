#Create project source code
###
git clone https://github.com/aesirteam/proxyRepeater.git
cd ./proxyRepeater
git clone https://github.com/aesirteam/srs-librtmp.git
./genMakefiles linux
make
#Run Program
###
./objs/testSrslibrtmp
#Clean project
###
make distclean
