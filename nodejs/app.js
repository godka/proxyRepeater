'use strict'

var nvr = require('bindings')('node_nvr_addon'),
EventEmitter = require('events').EventEmitter;

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

var event = new EventEmitter();

event.on('start', function(){
        nvr.start();
        console.log("start");
})

event.on('stop', function(){
        nvr.stop();
})

 nvr.init(JSON.stringify(data), function(err, msg){
        if(err) {
            console.error(msg);
            return;
        } 
        event.emit('start');
         event.emit('stop');
        //nvr.stop();
})