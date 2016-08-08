'use strict'

var nvr = require('../nodejs/build/Release/node_nvr_addon.node');

var data = [{
        url: "rtsp://10.196.230.149/000100",
        endpoint: "rtmp://127.0.0.1:1935/srs",
        stream: "stream3",
        password: "publish2016"
   }, {
        url: "rtsp://10.196.230.138:8554/cam",
        endpoint: "rtmp://127.0.0.1:1935/srs",
        stream: "stream4",
        password: "publish2016"
}]

 nvr.init(JSON.stringify(data), function(err, msg){
        if(err) {
            console.error(msg);
            return;
        } 
          nvr.start();
})