import { WebSocketServer } from 'ws';
import url from 'url';

const ws = new WebSocketServer({ port: 8585 });

const clients = {};

ws.on('connection', (socket, request)  => {
    const queryParams = url.parse(request.url, true).query;
    const source = request.headers['x-source'] ?? queryParams?.source;

    socket.source = source;

    clients[source] = socket;

    socket.send(JSON.stringify({ message: 'Connection Established' }));

    socket.on('error', console.error);

    socket.on('close', () => {
        delete clients[source];
    });

    socket.on('message', (rawData, isBinary) => {
        try {
            const data = JSON.parse(rawData);

            if (data.to && clients[data.to]) {
                const client = clients[data.to];

                client.send(rawData, { binary: isBinary });
            }

        } catch (error) {
            console.log(error, rawData);
        }
    });
});
