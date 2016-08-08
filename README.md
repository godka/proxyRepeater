#Create project source code
###
    git clone https://github.com/aesirteam/proxyRepeater.git
    cd ./proxyRepeater
    chmod +x genMakefiles
    ./genMakefiles linux
    make -j4
#Run Program
###
    ./objs/testSrslibrtmp
#Clean project
###
    make distclean
#Create nodejs addon(v 0.10.x)
###
    ./genMakefiles rpi
    make node
#Nodejs source sample
###
var nvr = require('../nodejs/build/Release/node_nvr_addon.node');
var data = [{
        url: "rtsp://10.196.230.149/000100",
        endpoint: "rtmp://127.0.0.1:1935/srs",
        stream: "stream1",
        password: "publish2016"
   }, {
        url: "rtsp://10.196.230.138:8554/cam",
        endpoint: "rtmp://127.0.0.1:1935/srs",
        stream: "stream2",
        password: "publish2016"
}]
 nvr.init(JSON.stringify(data), function(err, msg){
        if(err) {
            console.error(msg);
            return;
        } 
          nvr.start();
})