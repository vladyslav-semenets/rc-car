import { Injectable } from '@angular/core';

@Injectable({ providedIn: 'root' })
export class AppService {
  public raspberryPiIp = import.meta.env.NG_APP_RASPBERRY_PI_IP;
  public socket!: WebSocket;

  public initWebSocket(): void {
    this.socket = new WebSocket(`ws://${this.raspberryPiIp}:8585/?source=rc-car-client-map`);
    this.socket.binaryType = 'arraybuffer';
  }
}
