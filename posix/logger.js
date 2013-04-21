

var net = require('net');
var svr = net.createServer(function(c){
	console.log('client connected');

	c.on('data', function(data){
	    console.log(data.toString());
	    });

	c.on('end', function(){
	    console.log('client goodbye');
	    });

	});

svr.listen(4040, function(){
	console.log('server is listening...');
	});

