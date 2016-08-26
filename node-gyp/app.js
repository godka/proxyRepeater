var nvr = require('./build/Release/node_nvr_addon.node');

var data = [{
        url: "rtsp://10.196.230.149/000100",
        endpoint: "rtmp://117.135.196.137:11935/srs",
        stream: "stream1",
        password: "publish2016"
   }, {
        url: "rtsp://10.196.230.138:8554/cam",
        endpoint: "rtmp://117.135.196.137:11935/srs",
        stream: "stream2",
        password: "publish2016"
}];

nvr.init(JSON.stringify(data), function(err, msg){
	if(err) {
		console.error(msg);
		return;
	}
	nvr.start();
});